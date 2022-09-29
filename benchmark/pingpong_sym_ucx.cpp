#include "bench_fabric.hpp"
#include "comm_exp.hpp"
#include "thread_utils.hpp"
#include <vector>
#include <thread>
#include <boost/tokenizer.hpp>
#include <chrono>
#include <math.h>
#define _GNU_SOURCE // sched_getcpu(3) is glibc-specific (see the man page)
#include <sched.h>

using namespace boost;
using namespace fb;

int tx_thread_num = 4;
int rx_thread_num = 0;
int prefilled_work = 0;
int min_size = 8;
int max_size = 1024;
bool touch_data = false;
int rank, size, target_rank;
device_t device;

ctx_t *worker_ctxs;
addr_t *remote_send_ep_addrs;
bool show_extra_stats = true;
bool bind_prg_thread = true;
sync_t *syncs;
buffer_t * bufs;
std::atomic<bool> thread_stop = {false};

/* Callback to handle receive of active messages */
static ucs_status_t recv_msg_cb(void *arg, void *data, size_t length,
                                unsigned flags) {
    // dispatch to worker
    int worker_num = *((int *) arg);
    // We need to copy-out data and return UCS_OK if we want to use the data outside the callback
    char *buf = bufs[worker_num].buf;
    memcpy(buf, data, length);
    syncs[worker_num].sync++;
    return UCS_OK;
}

void *send_thread(void *arg) {
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    int cpu_num = sched_getcpu();
    fprintf(stderr, "Thread %3d is running on CPU %3d\n", thread_id, cpu_num);
    ctx_t &ctx = worker_ctxs[thread_id];
    char *s_buf = (char *) malloc(max_size);
    // each remote endpoint would only have one active message
    int dest_message_id = 0;
    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        isend_tag(ctx, s_buf, msg_size, dest_message_id);
        while (syncs[thread_id].sync == 0) {
            /* Explicitly progress any outstanding active message requests */
            uct_worker_progress(ctx.if_info.worker);
        };
        --syncs[thread_id].sync;
        }, {0, 1});

    // check whether the thread has migrated
    int cpu_num_final = sched_getcpu();
    if (cpu_num_final != cpu_num) {
        fprintf(stderr, "Thread %3d migrated to %3d\n", thread_id, cpu_num_final);
        exit(2);
    }
    return nullptr;
}

void *recv_thread(void *arg) {
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    ctx_t &ctx = worker_ctxs[thread_id];
    char *s_buf = (char *) malloc(max_size);
    int cpu_num = sched_getcpu();
    fprintf(stderr, "Thread %3d is running on CPU %3d\n", thread_id, cpu_num);

//         printf("I am %d, recving msg. iter first is %d, iter second is %d\n", rank,
//                (rank % (size / 2) * thread_count + thread_id),
//                ((size / 2) * thread_count));
    int dest_message_id = 0;

    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        while (syncs[thread_id].sync == 0) {
            /* Explicitly progress any outstanding active message requests */
            uct_worker_progress(ctx.if_info.worker);
        }
        --syncs[thread_id].sync;
        isend_tag(ctx, s_buf, msg_size, dest_message_id);
        }, {0, 1});

    // check whether the thread has migrated
    int cpu_num_final = sched_getcpu();
    if (cpu_num_final != cpu_num) {
        fprintf(stderr, "Thread %3d migrated to %3d\n", thread_id, cpu_num_final);
        exit(2);
    }
    return nullptr;
}

void reset_counters(int sync_count) {
    if (omp::thread_id() != 0) {
        // thread 0 is resetting syncs, counters etc for all workers
        return;
    }
    for (int i = 0; i < tx_thread_num; i++) {
        // reset syncs and time stats
        syncs[i].sync = sync_count;
    }
}

std::tuple<double, double, long long> get_additional_stats() {
}

int main(int argc, char *argv[]) {
    if (argc > 1)
        tx_thread_num = atoi(argv[1]);
    if (argc > 2)
        min_size = atoi(argv[2]);
    if (argc > 3)
        max_size = atoi(argv[3]);
    //printf("got all arguments");

    if (tx_thread_num * 2 * max_size > HEAP_SIZE) {
        printf("HEAP_SIZE is too small! (%d < %d required)\n", HEAP_SIZE, tx_thread_num * 2 * max_size);
        exit(1);
    }
    fprintf(stderr, "version: poll when worker idle\n");

    comm_init();
    ucs_status_t status;
    rank = pmi_get_rank();
    size = pmi_get_size();
    target_rank = (rank + size / 2) % size;
    // Send context (per worker)
    worker_ctxs = (ctx_t *) calloc(tx_thread_num, sizeof(ctx_t));
    remote_send_ep_addrs = (addr_t *) calloc(tx_thread_num, sizeof(addr_t));
    // recv buffers for each worker thread
    bufs = (buffer_t*) calloc(tx_thread_num, sizeof(buffer_t));
    totalExecTimes.resize(tx_thread_num);
    for (int i = 0; i < tx_thread_num; ++i) {
        init_ctx(&worker_ctxs[i], 1);
        put_ctx_addr(worker_ctxs[i], i, "w");
        // init recv buffer
        bufs[i].buf = (char*)calloc(max_size, sizeof(char));
    }
    flush_ctx_addr();
//    for (int i = 0; i < tx_thread_num; ++i) {
//        get_ctx_addr(worker_ctxs[i], target_rank, i, &remote_recv_ep_addrs[i], "w");
//        // test reachability to peer
//        status = (ucs_status_t) uct_iface_is_reachable(worker_ctxs[i].if_info.iface, remote_recv_ep_addrs[i].peer_dev,
//                                                       remote_recv_ep_addrs[i].peer_iface);
//        CHKERR_MSG(0 == status, "cannot reach the peer");
//        if (status == 0) {
//            std::cout << "peer device unreachable!" << std::endl;
//            exit(1);
//        }
//    }
    // get remote send ctx address
    for (int i = 0; i < tx_thread_num; ++i) {
        get_ctx_addr(worker_ctxs[i], target_rank, i, &remote_send_ep_addrs[i], "w");
        assert(worker_ctxs[i].eps.size() == 1);
    }

    // connect the endpoints
    for (int i = 0; i < tx_thread_num; ++i) {
        auto& remote_ep_info = remote_send_ep_addrs[i] ;
        // connecting send ep with remote send thread
        assert(worker_ctxs[i].eps.size() == 1);
        uct_ep_connect_to_ep(worker_ctxs[i].eps[0], remote_ep_info.peer_dev, remote_ep_info.peer_ep_addrs[0]);
    }
    pmi_barrier();
    // Set active message handler(per worker thread)
    for (int i = 0; i < tx_thread_num; i++) {
        auto& ctx = worker_ctxs[i];
        // disable async callback. IB verbs API doesn't support that anyway
        const int callback_flag = 0;
        int* id = (int*) malloc(sizeof(int));
        *id = i;
        const int ACTIVE_MESSAGE_ID = 0;
        status = uct_iface_set_am_handler(ctx.if_info.iface, ACTIVE_MESSAGE_ID, recv_msg_cb,
                                          id, callback_flag);
        CHKERR_MSG(UCS_OK != status, "can't set callback");
    }

    // atomic flag(per worker) used by the progress thread to signal that the worker can read from the receive buf
    syncs = (sync_t*) calloc(tx_thread_num, sizeof(sync_t));
    for (int i = 0; i < tx_thread_num; ++i) {
        syncs[i].sync = 0;
    }
    if (rank < size / 2) {
        omp::thread_run(send_thread, tx_thread_num);
    } else {
        omp::thread_run(recv_thread, tx_thread_num);
    }
    thread_stop = true;
    // clean up resources
    free(worker_ctxs);
    comm_free();
    return 0;
}
