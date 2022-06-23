#include "bench_fabric.hpp"
#include "comm_exp.hpp"
#include "thread_utils.hpp"
#define _GNU_SOURCE // sched_getcpu(3) is glibc-specific (see the man page)

#include <sched.h>
using namespace fb;

int thread_num = 4;
int rx_thread_num = 0;
int prefilled_work = 0;
int min_size = 8;
int max_size = 64 * 1024;
bool show_extra_stats = false;
bool touch_data = false;
int rank, size, target_rank;
device_t device;
cq_t *send_cqs;
cq_t *recv_cqs;

ctx_t *ctxs;
addr_t *addrs;

const int NUM_PREPOST_RECV = RX_QUEUE_LEN - 3;

void prepost_recv(int thread_id) {
    ctx_t &ctx = ctxs[thread_id];
    addr_t &addr = addrs[thread_id];
    char *buf = (char *) device.heap_ptr + thread_id * max_size;
    irecv(ctx, buf, max_size, addr, NUM_PREPOST_RECV);
}

// not used in pingpong_sym
void reset_counters(int sync_count) {}
std::tuple<double, double, long long> get_additional_stats() {}

void *send_thread(void *arg) {
    //printf("I am a send thread\n");
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    int cpu_num = sched_getcpu();
    fprintf(stderr, "Thread %3d is running on CPU %3d\n", thread_id, cpu_num);
    cq_t &send_cq = send_cqs[thread_id];
    cq_t &recv_cq = recv_cqs[thread_id];
    ctx_t &ctx = ctxs[thread_id];
    addr_t& addr = addrs[thread_id];
    char *buf = (char *) device.heap_ptr + thread_id * max_size;

//     printf("I am %d, sending msg. iter first is %d, iter second is %d\n", rank,
//            (rank % (size / 2) * thread_count + thread_id),
//            ((size / 2) * thread_count));
    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        isend(ctx, buf, msg_size, addr);
        while (!progress(send_cq));
        irecv(ctx, buf, max_size, addr, 1);
        while (!progress(recv_cq));
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
    char *buf = (char *) device.heap_ptr + thread_id * max_size;
    addr_t& addr = addrs[thread_id];
//     printf("I am %d, recving msg. iter first is %d, iter second is %d\n", rank,
//            (rank % (size / 2) * thread_count + thread_id),
//            ((size / 2) * thread_count));
    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        irecv(ctx, buf, max_size, addr, 1);
        while (!progress(recv_cq));
        isend(ctx, buf, msg_size, addr);
        while (!progress(send_cq));
    }, {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});

    return nullptr;
}

int main(int argc, char *argv[]) {
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
    comm_init();
    init_device(&device, thread_num != 1);
    rank = pmi_get_rank();
    size = pmi_get_size();
    target_rank = (rank + size / 2) % size;

    // separate completion queues for send and receive
    send_cqs = (cq_t *) calloc(thread_num, sizeof(cq_t));
    recv_cqs = (cq_t *) calloc(thread_num, sizeof(cq_t));
    ctxs = (ctx_t *) calloc(thread_num, sizeof(ctx_t));
    addrs = (addr_t *) calloc(thread_num, sizeof(addr_t));

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
        init_ctx(&device, send_cqs[i], recv_cqs[i], srq_t(), &ctxs[i], CTX_TX | CTX_RX);
        put_ctx_addr(ctxs[i], i);
    }
    flush_ctx_addr();
    for (int i = 0; i < thread_num; ++i) {
        get_ctx_addr(device, target_rank, i, &addrs[i]);
    }
    // Put the queue pairs to the correct states
    for (int i = 0; i < thread_num; i++) {
        connect_ctx(ctxs[i], addrs[i]);
    }
    // prepost recv here
    for (int i = 0; i < thread_num; i++) {
        prepost_recv(i);
    }
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
