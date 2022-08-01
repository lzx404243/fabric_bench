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
int min_size = 8;
int max_size = 1024;
bool touch_data = false;
int rank, size, target_rank;
device_t device;

ctx_t *worker_ctxs;
ctx_t *progress_ctxs;
addr_t *remote_recv_ep_addrs;
addr_t *remote_send_ep_addrs;
counter_t *next_worker_idxes;
bool show_extra_stats = true;
bool bind_prg_thread = true;
sync_t *syncs;
buffer_t * bufs;
std::atomic<bool> thread_stop = {false};
std::atomic<int> thread_started = {0};
int rx_thread_num = 1;
time_acc_t * compute_time_accs;
counter_t *progress_counters;
time_acc_t * idle_time_accs;

std::vector<int> prg_thread_bindings;
int compute_time_in_us = 0;
int prefilled_work = 0;
int num_worker_per_qp = 1;
int works_per_progress_thread = -1;

/* Callback to handle receive of active messages */
static ucs_status_t recv_msg_cb(void *arg, void *data, size_t length,
                                unsigned flags) {
    // dispatch to worker
    int prg_thread_id = *((int *) arg);
    auto &worker_num = next_worker_idxes[prg_thread_id].count;
    //We need to copy-out data and return UCS_OK if we want to use the data outside the callback
    char *buf = bufs[worker_num].buf;
    memcpy(buf, data, length);
    syncs[worker_num].sync++;
    // update next worker in a round-robin manner
    worker_num += rx_thread_num;
    if (worker_num >= tx_thread_num) {
        worker_num = prg_thread_id;
    }
    return UCS_OK;
}

void *send_thread(void *arg) {
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    int cpu_num = sched_getcpu();
    fprintf(stderr, "Thread %3d is running on CPU %3d\n", thread_id, cpu_num);
    ctx_t &ctx = worker_ctxs[thread_id];
    char *s_buf = (char *) malloc(max_size);
    time_acc_t &time_acc = compute_time_accs[thread_id];
    time_acc_t &idle_time_acc = idle_time_accs[thread_id];
    bool idled = false;
    struct timespec start, stop;
//         printf("I am %d, sending msg. iter first is %d, iter second is %d\n", rank,
//                (rank % (size / 2) * thread_count + thread_id),
//                ((size / 2) * thread_count));
    int dest_message_id = thread_id % rx_thread_num;
    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        isend_tag(ctx, s_buf, msg_size, dest_message_id);
        // progress for send completion. Note that we don't block  for the completion of the send here
        while (syncs[thread_id].sync == 0) {
            // idle
            if (!idled) {
                // start timer
                idled = true;
                clock_gettime(CLOCK_REALTIME, &start);
            }
        }
        if (idled) {
            // stop timer
            clock_gettime(CLOCK_REALTIME, &stop);
            idle_time_acc.tot_time_us += ( stop.tv_nsec - start.tv_nsec ) / 1e3
                    + ( stop.tv_sec - start.tv_sec ) * 1e6;
            idled = false;
        }
        --syncs[thread_id].sync;
        // compute
        if (compute_time_in_us > 0) {
            sleep_for_us(compute_time_in_us, time_acc);
        }
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
    time_acc_t &time_acc = compute_time_accs[thread_id];
    time_acc_t &idle_time_acc = idle_time_accs[thread_id];
    bool idled = false;
    struct timespec start, stop;
    fprintf(stderr, "Thread %3d is running on CPU %3d\n", thread_id, cpu_num);

//         printf("I am %d, recving msg. iter first is %d, iter second is %d\n", rank,
//                (rank % (size / 2) * thread_count + thread_id),
//                ((size / 2) * thread_count));
    int dest_message_id = thread_id % rx_thread_num;

    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        while (syncs[thread_id].sync == 0) {
            // idle
            if (!idled) {
                // start timer
                idled = true;
                clock_gettime(CLOCK_REALTIME, &start);
            }
        }
        if (idled) {
            // stop timer
            clock_gettime(CLOCK_REALTIME, &stop);
            idle_time_acc.tot_time_us += ( stop.tv_nsec - start.tv_nsec ) / 1e3
                    + ( stop.tv_sec - start.tv_sec ) * 1e6;
            idled = false;
        }

        --syncs[thread_id].sync;
        // compute
        if (compute_time_in_us > 0) {
            sleep_for_us(compute_time_in_us, time_acc);
        }
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

void progress_thread(int id) {
    if (bind_prg_thread) {
        // bind progress thread to core using the input binding specification if available
        auto err = comm_set_me_to(!prg_thread_bindings.empty() ? prg_thread_bindings[id] : 15);
        if (err) {
            errno = err;
            printf("setting progress thread affinity failed: error %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    int cpu_num = sched_getcpu();
    fprintf(stderr, "wall clock progress Thread is running on CPU %3d\n",  cpu_num);
    thread_started++;
    auto& ctx = progress_ctxs[id];
    while (!thread_stop.load()) {
        /* Explicitly progress any outstanding active message requests */
        uct_worker_progress(ctx.if_info.worker);
    }
}

void reset_counters(int sync_count) {
    if (omp::thread_id() != 0) {
        // thread 0 is resetting syncs, counters etc for all workers
        return;
    }
    for (int i = 0; i < tx_thread_num; i++) {
        // reset syncs and time stats
        syncs[i].sync = sync_count;
        compute_time_accs[i].tot_time_us = 0;
        idle_time_accs[i].tot_time_us = 0;
    }
    for (int i = 0; i < rx_thread_num; i++) {
        // for all progress thread, reset next worker index to the first worker it handles
        next_worker_idxes[i].count = i;
        // reset progress counters
        progress_counters[i].count = 0;
    }
}

std::tuple<double, double, long long> get_additional_stats() {
    return {get_overhead(compute_time_accs), get_overhead(idle_time_accs), get_progress_total(progress_counters)};
}

int main(int argc, char *argv[]) {
    if (argc > 1)
        tx_thread_num = atoi(argv[1]);
    if (argc > 2)
        rx_thread_num = atoi(argv[2]);
    if (argc > 3)
        min_size = atoi(argv[3]);
    if (argc > 4)
        max_size = atoi(argv[4]);
    if (argc > 5) {
        // comma-seperated list to specify the thread each progressive thread is going to bind to
        std::string core_list(argv[5]);
        char_separator<char> sep(",");
        tokenizer<char_separator<char>> tokens(core_list, sep);
        for (auto t : tokens) {
            prg_thread_bindings.push_back(std::stoi(t));
        }
        // make sure the number of bindings equal to the number of progress threads
        if (prg_thread_bindings.size() != rx_thread_num) {
            fprintf(stderr, "number of binding specification doesn't match the number of progress thread.\n");
            exit(EXIT_FAILURE);
        }
    }
    if (argc > 6) {
        compute_time_in_us = atoi(argv[6]);
    }
    if (argc > 7) {
        prefilled_work = atoi(argv[7]);
    }
    if (argc > 8) {
        num_worker_per_qp = atoi(argv[8]);
        if (num_worker_per_qp > floor(tx_thread_num / rx_thread_num)) {
            printf("Number of worker sharing one queue pair is too large. max is: %d\n", floor(tx_thread_num / rx_thread_num));
            exit(EXIT_FAILURE);
        }
    }
    // check worker thread distribution
    works_per_progress_thread =  tx_thread_num / rx_thread_num;
    if (tx_thread_num % rx_thread_num != 0) {
        printf("number of worker needs to be divisible by number of progress thread. If this is not true, need to change the core binding before running and among other things\n");
        exit(EXIT_FAILURE);
    }
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
    // prg thread context
    progress_ctxs = (ctx_t *) calloc(rx_thread_num, sizeof(ctx_t));
    remote_recv_ep_addrs = (addr_t *) calloc(rx_thread_num, sizeof(addr_t));
    // Accumulators for compute time for each thread
    compute_time_accs = (time_acc_t *) calloc(tx_thread_num, sizeof(time_acc_t));
    // counters for progress for each progress thread
    progress_counters = (counter_t *) calloc(tx_thread_num, sizeof(counter_t));
    // counters for messages for each progress thread
    next_worker_idxes = (counter_t *) calloc(rx_thread_num, sizeof(counter_t));
    // Accumulators for idle time for each thread
    idle_time_accs = (time_acc_t *) calloc(tx_thread_num, sizeof(time_acc_t));
    // recv buffers for each worker thread
    bufs = (buffer_t*) calloc(tx_thread_num, sizeof(buffer_t));
    for (int i = 0; i < rx_thread_num; ++i) {
        init_ctx(&progress_ctxs[i], works_per_progress_thread);
        put_ctx_addr(progress_ctxs[i], i, "p");
        // initialize worker index
        next_worker_idxes[i].count = i;
    }
    for (int i = 0; i < tx_thread_num; ++i) {
        init_ctx(&worker_ctxs[i], 1);
        put_ctx_addr(worker_ctxs[i], i, "w");
        // init recv buffer
        bufs[i].buf = (char*)calloc(max_size, sizeof(char));
    }
    flush_ctx_addr();
    for (int i = 0; i < rx_thread_num; ++i) {
        get_ctx_addr(progress_ctxs[i], target_rank, i, &remote_recv_ep_addrs[i], "p");
        // test reachability to peer
        status = (ucs_status_t) uct_iface_is_reachable(progress_ctxs[i].if_info.iface, remote_recv_ep_addrs[i].peer_dev,
                                                       remote_recv_ep_addrs[i].peer_iface);
        CHKERR_MSG(0 == status, "cannot reach the peer");
        if (status == 0) {
            std::cout << "peer device unreachable!" << std::endl;
            exit(1);
        }
    }
    // get remote send ctx address
    for (int i = 0; i < tx_thread_num; ++i) {
        get_ctx_addr(worker_ctxs[i], target_rank, i, &remote_send_ep_addrs[i], "w");
        assert(worker_ctxs[i].eps.size() == 1);
    }

    // connect the endpoints
    for (int i = 0; i < tx_thread_num; ++i) {
        auto& remote_ep_info = remote_recv_ep_addrs[i % rx_thread_num] ;
        // connecting send ep with remote prgress thread
        assert(worker_ctxs[i].eps.size() == 1);
        uct_ep_connect_to_ep(worker_ctxs[i].eps[0], remote_ep_info.peer_dev, remote_ep_info.peer_ep_addrs[i / rx_thread_num]);
    }
    for (int i = 0; i < rx_thread_num; ++i) {
        assert(progress_ctxs[i].eps.size() == works_per_progress_thread);
        for (int j = i; j < tx_thread_num; j+= rx_thread_num) {
            // connecting progress thread with remote send ep
            auto& remote_send_ep_info = remote_send_ep_addrs[j];
            uct_ep_connect_to_ep(progress_ctxs[i].eps[j / rx_thread_num], remote_send_ep_info.peer_dev, remote_send_ep_info.peer_ep_addrs[0]);
        }
    }
    pmi_barrier();
    // Set active message handler(per progress thread)
    for (int i = 0; i < rx_thread_num; i++) {
        auto& prg_ctx = progress_ctxs[i];
        // disable async callback. IB verbs API doesn't support that anyway
        const int callback_flag = 0;
        int* id = (int*) malloc(sizeof(int));
        *id = i;
        status = uct_iface_set_am_handler(prg_ctx.if_info.iface, i, recv_msg_cb,
                                          id, callback_flag);
        CHKERR_MSG(UCS_OK != status, "can't set callback");
    }

    // atomic flag(per worker) used by the progress thread to signal that the worker can read from the receive buf
    syncs = (sync_t*) calloc(tx_thread_num, sizeof(sync_t));
    for (int i = 0; i < tx_thread_num; ++i) {
        syncs[i].sync = 0;
    }

    std::vector<std::thread> threads(rx_thread_num);
    for (int id = 0; id < rx_thread_num; id++) {
        threads[id] = std::thread(progress_thread, id);
    }
    // wait for progress threads set up
    while (thread_started.load() != rx_thread_num) continue;
    if (rank < size / 2) {
        omp::thread_run(send_thread, tx_thread_num);
    } else {
        omp::thread_run(recv_thread, tx_thread_num);
    }
    thread_stop = true;
    for (int id = 0; id < rx_thread_num; id++) {
        threads[id].join();
    }
    // clean up resources
    free(remote_recv_ep_addrs);
    free(worker_ctxs);
    comm_free();
    return 0;
}
