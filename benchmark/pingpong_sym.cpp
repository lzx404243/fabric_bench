#include "bench_fabric.hpp"
#include "comm_exp.hpp"
#include "thread_utils.hpp"
// todo: remove this include before compiling. For now this include is meant for work around with a syntax highlighting issue

#include "bench_ib.hpp"
#define _GNU_SOURCE // sched_getcpu(3) is glibc-specific (see the man page)

#include <sched.h>
using namespace fb;

int thread_num = 4;
int min_size = 8;
int max_size = 64 * 1024;
bool touch_data = false;
int rank, size, target_rank;
device_t device;
cq_t *send_cqs;
cq_t *recv_cqs;

ctx_t *ctxs;
addr_t *addrs;
reqs_t *reqs;

const int NUM_PREPOST_RECV = RX_QUEUE_LEN - 3;
//const int NUM_PREPOST_RECV = 0;

omp_lock_t writelock;

// time measurement for the loops
std::vector<std::vector<double>> checkpointTimesAll;
std::vector<std::vector<double>> checkpointTimesAllSkip;
std::vector<double> totalExecTimes;

void prepost_recv(int thread_id) {
    ctx_t &ctx = ctxs[thread_id];
    char *buf = (char *) device.heap_ptr + thread_id * max_size;
    req_t& req_recv = reqs[thread_id].req_recv;
    for (int i = 0; i < NUM_PREPOST_RECV; i++) {
        irecv_tag(ctx, buf, 8, addrs[thread_id], 0, &req_recv);
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

    ctx_t &ctx = ctxs[thread_id];
    // todo: zheli -- the following is a hack to specify the thread as a sending thread
    ctx.mode = CTX_TX;
    char *buf = (char *) device.heap_ptr + thread_id * max_size;
    char s_data = rank * thread_count + thread_id;
    char r_data = target_rank * thread_count + thread_id;
    req_t& req_send = reqs[thread_id].req_send;
    req_t& req_recv = reqs[thread_id].req_recv;

    // printf("I am %d, sending msg. iter first is %d, iter second is %d\n", rank,
    //        (rank % (size / 2) * thread_count + thread_id),
    //        ((size / 2) * thread_count));
    //printf("Setting qp to correct state");
    connect_ctx(ctx, addrs[thread_id]);
    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        isend_tag(ctx, buf, msg_size, addrs[thread_id], thread_id, &req_send);
        while (req_send.type != REQ_TYPE_NULL) progress(send_cq);
        irecv_tag(ctx, buf, msg_size, addrs[thread_id], thread_id, &req_recv);
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

    ctx_t &ctx = ctxs[thread_id];
    ctx.mode = CTX_RX;

    char *buf = (char *) device.heap_ptr + thread_id * max_size;
    char s_data = rank * thread_count + thread_id;
    char r_data = target_rank * thread_count + thread_id;
    req_t& req_send = reqs[thread_id].req_send;
    req_t& req_recv = reqs[thread_id].req_recv;
    // printf("I am %d, recving msg. iter first is %d, iter second is %d\n", rank,
    //        (rank % (size / 2) * thread_count + thread_id),
    //        ((size / 2) * thread_count));
    //printf("Setting qp to correct state");
    connect_ctx(ctx, addrs[thread_id]);
    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        irecv_tag(ctx, buf, msg_size, addrs[thread_id], thread_id, &req_recv);
        while (req_recv.type != REQ_TYPE_NULL) progress(recv_cq);
        isend_tag(ctx, buf, msg_size, addrs[thread_id], thread_id, &req_send);
        while (req_send.type != REQ_TYPE_NULL) progress(send_cq);
    }, {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});

    return nullptr;
}


int main(int argc, char *argv[]) {
    fprintf(stderr, "pingpong_sym from summer TOTAL+skip");

    if (argc > 1)
        thread_num = atoi(argv[1]);
    if (argc > 2)
        min_size = atoi(argv[2]);
    if (argc > 3)
        max_size = atoi(argv[3]);
    //printf("got all arguments");
    if (thread_num * max_size > HEAP_SIZE) {
        printf("HEAP_SIZE is too small! (%d < %d required)\n", HEAP_SIZE, thread_num * max_size);
        exit(1);
    }
    //printf("calling comm_init\n");
    comm_init();
    //printf("comm inited\n");

    init_device(&device, thread_num != 1);
    //printf("device inited\n");

    rank = pmi_get_rank();
    size = pmi_get_size();
    target_rank = (rank + size / 2) % size;

    // separate completion queues for send and receive
    send_cqs = (cq_t *) calloc(thread_num, sizeof(cq_t));
    recv_cqs = (cq_t *) calloc(thread_num, sizeof(cq_t));
    ctxs = (ctx_t *) calloc(thread_num, sizeof(ctx_t));
    addrs = (addr_t *) calloc(thread_num, sizeof(addr_t));
    reqs = (reqs_t*) calloc(thread_num, sizeof(reqs_t));

    // time measurement for the loops
    checkpointTimesAll.resize(thread_num);
    checkpointTimesAllSkip.resize(thread_num);
    totalExecTimes.resize(thread_num);
    // omp mutex
    omp_init_lock(&writelock);

    //printf("init per thread structures\n");
    for (int i = 0; i < thread_num; ++i) {
        init_cq(device, &send_cqs[i]);
        init_cq(device, &recv_cqs[i]);
        init_ctx(&device, send_cqs[i], recv_cqs[i], &ctxs[i], CTX_TX | CTX_RX);
        put_ctx_addr(ctxs[i], i);
    }
    flush_ctx_addr();
    for (int i = 0; i < thread_num; ++i) {
        get_ctx_addr(device, target_rank, i, &addrs[i]);
    }
    // Put the queue pairs to the correct states
    //    for (int i = 0; i < thread_num; i++) {
    //        connect_ctx(ctxs[i], addrs[i]);
    //    }
    if (rank < size / 2) {
        omp::thread_run(send_thread, thread_num);
    } else {
        omp::thread_run(recv_thread, thread_num);
    }

    //printf("Done: Freeing resources\n");
    for (int i = 0; i < thread_num; ++i) {
        free_ctx(&ctxs[i]);
        free_cq(&send_cqs[i]);
        free_cq(&recv_cqs[i]);
    }
    omp_destroy_lock(&writelock);

    free_device(&device);
    free(addrs);
    free(ctxs);
    free(send_cqs);
    free(recv_cqs);
    comm_free();
    return 0;
}
