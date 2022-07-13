#include "bench_fabric.hpp"
#include "comm_exp.hpp"
#include "thread_utils.hpp"
#define _GNU_SOURCE // sched_getcpu(3) is glibc-specific (see the man page)

#include <sched.h>
using namespace fb;

int tx_thread_num = 4;
int min_size = 8;
int max_size = 64 * 1024;
bool touch_data = false;
int rank, size, target_rank;
device_t device;
cq_t *send_cqs;
cq_t *recv_cqs;

ctx_t *tx_ctxs;
addr_t *addrs;
reqs_t *reqs;

const int NUM_PREPOST_RECV = RX_QUEUE_LEN - 3;
//const int NUM_PREPOST_RECV = 0;

void prepost_recv(int thread_id) {
    ctx_t &ctx = tx_ctxs[thread_id];
    char *buf = (char *) device.heap_ptr + thread_id * max_size;
    req_t& req_recv = reqs[thread_id].req_recv;
    for (int i = 0; i < NUM_PREPOST_RECV; i++) {
        irecv(ctx, buf, 8, &req_recv);
    }
}



void *send_thread(void *arg) {
    //printf("I am a send thread\n");
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    int cpu_num = sched_getcpu();
    fprintf(stderr, "Thread %3d is running on CPU %3d\n", thread_id, cpu_num);
    cq_t &send_cq = send_cqs[thread_id];
    cq_t &recv_cq = recv_cqs[thread_id];

    ctx_t &ctx = tx_ctxs[thread_id];
    // todo: zheli -- the following is a hack to specify the thread as a sending thread
    char *buf = (char *) device.heap_ptr + thread_id * max_size;
    char s_data = rank * thread_count + thread_id;
    char r_data = target_rank * thread_count + thread_id;
    req_t& req_send = reqs[thread_id].req_send;
    req_t& req_recv = reqs[thread_id].req_recv;

    // printf("I am %d, sending msg. iter first is %d, iter second is %d\n", rank,
    //        (rank % (size / 2) * thread_count + thread_id),
    //        ((size / 2) * thread_count));
    //printf("Setting qp to correct state");
    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        isend(ctx, buf, msg_size, &req_send);
        while (req_send.type != REQ_TYPE_NULL) progress(send_cq);
        irecv(ctx, buf, msg_size, &req_recv);
        while (req_recv.type != REQ_TYPE_NULL) progress(recv_cq);
    }, {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});
    return nullptr;
}

void *recv_thread(void *arg) {
    //printf("I am a recv thread\n");
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    cq_t &send_cq = send_cqs[thread_id];
    cq_t &recv_cq = recv_cqs[thread_id];

    ctx_t &ctx = tx_ctxs[thread_id];
    char *buf = (char *) device.heap_ptr + thread_id * max_size;
    char s_data = rank * thread_count + thread_id;
    char r_data = target_rank * thread_count + thread_id;
    req_t& req_send = reqs[thread_id].req_send;
    req_t& req_recv = reqs[thread_id].req_recv;
    // printf("I am %d, recving msg. iter first is %d, iter second is %d\n", rank,
    //        (rank % (size / 2) * thread_count + thread_id),
    //        ((size / 2) * thread_count));
    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        irecv(ctx, buf, msg_size, &req_recv);
        while (req_recv.type != REQ_TYPE_NULL) progress(recv_cq);
        isend(ctx, buf, msg_size, &req_send);
        while (req_send.type != REQ_TYPE_NULL) progress(send_cq);
    }, {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});

    return nullptr;
}


int main(int argc, char *argv[]) {
    fprintf(stderr, "pingpong_sym from summer TOTAL+skip");

    if (argc > 1)
        tx_thread_num = atoi(argv[1]);
    if (argc > 2)
        min_size = atoi(argv[2]);
    if (argc > 3)
        max_size = atoi(argv[3]);
    //printf("got all arguments");
    if (tx_thread_num * max_size > HEAP_SIZE) {
        printf("HEAP_SIZE is too small! (%d < %d required)\n", HEAP_SIZE, tx_thread_num * max_size);
        exit(1);
    }
    //printf("calling comm_init\n");
    comm_init();
    //printf("comm inited\n");

    init_device(&device, tx_thread_num != 1);
    //printf("device inited\n");

    rank = pmi_get_rank();
    size = pmi_get_size();
    target_rank = (rank + size / 2) % size;

    // separate completion queues for send and receive
    send_cqs = (cq_t *) calloc(tx_thread_num, sizeof(cq_t));
    recv_cqs = (cq_t *) calloc(tx_thread_num, sizeof(cq_t));
    tx_ctxs = (ctx_t *) calloc(tx_thread_num, sizeof(ctx_t));
    addrs = (addr_t *) calloc(tx_thread_num, sizeof(addr_t));
    reqs = (reqs_t*) calloc(tx_thread_num, sizeof(reqs_t));

    // time measurement for the loops
    checkpointTimesAll.resize(tx_thread_num);
    checkpointTimesAllSkip.resize(tx_thread_num);
    totalExecTimes.resize(tx_thread_num);
    // omp mutex
    omp_init_lock(&writelock);

    //printf("init per thread structures\n");
    for (int i = 0; i < tx_thread_num; ++i) {
        init_cq(device, &send_cqs[i]);
        init_cq(device, &recv_cqs[i]);
        init_ctx(&device, send_cqs[i], recv_cqs[i], srq_t(), &tx_ctxs[i]);
        put_ctx_addr(tx_ctxs[i], i);
    }
    flush_ctx_addr();
    for (int i = 0; i < tx_thread_num; ++i) {
        get_ctx_addr(device, target_rank, i, &addrs[i]);
    }
    // Put the queue pairs to the correct states
    for (int i = 0; i < tx_thread_num; i++) {
        connect_ctx(tx_ctxs[i], addrs[i]);
    }

    if (rank < size / 2) {
        omp::thread_run(send_thread, tx_thread_num);
    } else {
        omp::thread_run(recv_thread, tx_thread_num);
    }

    //printf("Done: Freeing resources\n");
    for (int i = 0; i < tx_thread_num; ++i) {
        free_ctx(&tx_ctxs[i]);
        free_cq(&send_cqs[i]);
        free_cq(&recv_cqs[i]);
    }
    omp_destroy_lock(&writelock);

    free_device(&device);
    free(addrs);
    free(tx_ctxs);
    free(send_cqs);
    free(recv_cqs);
    comm_free();
    return 0;
}
