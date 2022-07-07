#include "bench_fabric.hpp"
#include "thread_utils.hpp"
#include "comm_exp.hpp"
#include <atomic>
#include <thread>
#include <vector>
#include <boost/tokenizer.hpp>


using namespace fb;
using namespace boost;

int min_size = 8;
int max_size = 8192;
const bool bind_prg_thread = true;
int rank, size, target_rank;
device_t device;
cq_t *tx_cqs;
cq_t *rx_cqs;
ctx_t *tx_ctxs;
ctx_t *rx_ctxs;
addr_t *addrs;
sync_t *syncs;
std::atomic<bool> thread_stop = {false};
std::atomic<int> thread_started = {0};
time_acc_t * compute_time_accs;
time_acc_t * idle_time_accs;
counter_t *progress_counters;
counter_t *next_worker_idxes;


std::vector<int> prg_thread_bindings;
int compute_time_in_us = 0;
int prefilled_work = 0;

// todo: may refractor to a preSKIP_hook()
void prepost_recv(int thread_id) {

}

void reset_counters() {
    for (int i = 0; i < tx_thread_num; i++) {
        // reset syncs
        syncs[i].sync = prefilled_work;
    }
    for (int i = 0; i < rx_thread_num; i++) {
        // for all progress thread, reset next worker index to the first worker it handles
        next_worker_idxes[i].count = i;
    }
}

void* send_thread(void* arg) {
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    addr_t& addr = addrs[thread_id % rx_thread_num];
    ctx_t& tx_ctx = tx_ctxs[thread_id];
    auto& cq = tx_cqs[thread_id];

    char *s_buf = (char*) device.heap_ptr + thread_id * 2 * max_size;
    char *r_buf = (char*) device.heap_ptr + (thread_id * 2 + 1) * max_size;
    char s_data = rank * thread_count + thread_id;
    char r_data = target_rank * thread_count + thread_id;
    req_t req = {REQ_TYPE_NULL};
    time_acc_t &time_acc = compute_time_accs[thread_id];
    time_acc_t &idle_time_acc = idle_time_accs[thread_id];
    bool idled = false;
    struct timespec start, stop;

    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        isend(tx_ctx, s_buf, msg_size, addr, 0, &req);
        progress(cq);
        while (syncs[thread_id].sync == 0) {
            // idle
            if (!idled) {
                // start timer
                idled = true;
                clock_gettime(CLOCK_REALTIME, &start);
            }
            // progress for send completion. Note that we don't block for the completion of the send here
            progress(cq);
        }
        if (idled) {
            // stop timer
            clock_gettime(CLOCK_REALTIME, &stop);
            idle_time_acc.tot_time_us += ( stop.tv_nsec - start.tv_nsec ) / 1e3
                                         + ( stop.tv_sec - start.tv_sec ) * 1e6;
            idled = false;
        }
        --syncs[thread_id].sync;
        if (compute_time_in_us > 0) {
            sleep_for_us(compute_time_in_us, time_acc);
        }
    }, {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});

    return nullptr;
}

void* recv_thread(void* arg) {
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    addr_t& addr = addrs[thread_id % rx_thread_num];
    ctx_t& tx_ctx = tx_ctxs[thread_id];
    char *s_buf = (char*) device.heap_ptr + thread_id * 2 * max_size;
    char *r_buf = (char*) device.heap_ptr + (thread_id * 2 + 1) * max_size;
    char s_data = rank * thread_count + thread_id;
    char r_data = target_rank * thread_count + thread_id;
    auto& cq = tx_cqs[thread_id];
    req_t req = {REQ_TYPE_NULL};
    time_acc_t &time_acc = compute_time_accs[thread_id];
    time_acc_t &idle_time_acc = idle_time_accs[thread_id];
    bool idled = false;
    struct timespec start, stop;

    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        while (syncs[thread_id].sync == 0) {
            // idle
            if (!idled) {
                // start timer
                idled = true;
                clock_gettime(CLOCK_REALTIME, &start);
            }
            // progress for send completion. Note that we don't block for the completion of the send here
            progress(cq);
        }
        if (idled) {
            // stop timer
            clock_gettime(CLOCK_REALTIME, &stop);
            idle_time_acc.tot_time_us += ( stop.tv_nsec - start.tv_nsec ) / 1e3
                                         + ( stop.tv_sec - start.tv_sec ) * 1e6;
            idled = false;
        }
        --syncs[thread_id].sync;
        if (compute_time_in_us > 0) {
            sleep_for_us(compute_time_in_us, time_acc);
        }
        isend(tx_ctx, s_buf, msg_size, addr, 0, &req);
        progress(cq);
    }, {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});

    return nullptr;
}

void incrementWorkerIdx(int progress_thread_id, long long& workerIdx) {
    workerIdx += rx_thread_num;
    if (workerIdx >= tx_thread_num) {
        workerIdx = progress_thread_id;
    }
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
    ctx_t& rx_ctx = rx_ctxs[id];
    // todo: remove the reqs. not needed
    req_t *reqs = (req_t*) calloc(tx_thread_num, sizeof(req_t));
    long long& progress_counter = progress_counters[id].count;
    long long& worker_idx = next_worker_idxes[id].count;
    // first worker id
    worker_idx = id;
    progress_counter = 0;

    const int NUM_PREPOST = 2048;
    char *buf = (char*) device.heap_ptr + (2 * worker_idx + 1) * max_size;
    // all using tag 0
    irecv(rx_ctx, buf, max_size, ADDR_ANY, 0, &reqs[0], NUM_PREPOST);
    thread_started++;
    while (!thread_stop.load()) {
        int numCompleted = progress(rx_cqs[id]);
        for (int k = 0; k < numCompleted; k++) {
            // assign work to workers in a round-robin manner
            ++syncs[worker_idx].sync;
            incrementWorkerIdx(id, worker_idx);
        }
        char* buf = (char *) device.heap_ptr + (2 * worker_idx + 1) * max_size;
        irecv(rx_ctx, buf, max_size, ADDR_ANY, 0, &reqs[0], numCompleted);
    }
    free(reqs);
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
        // make sure the number of bindings equal to the number of progress thread
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

    if (tx_thread_num * 2 * max_size > HEAP_SIZE){
        printf("HEAP_SIZE is too small! (%d < %d required)\n", HEAP_SIZE, tx_thread_num * 2 * max_size);
        exit(1);
    }
    comm_init();
    init_device(&device, true);
    rank = pmi_get_rank();
    size = pmi_get_size();
    target_rank = (rank + size / 2) % size;

    tx_cqs = (cq_t*) calloc(tx_thread_num, sizeof(cq_t));
    rx_cqs = (cq_t*) calloc(rx_thread_num, sizeof(cq_t));
    tx_ctxs = (ctx_t*) calloc(tx_thread_num, sizeof(ctx_t));
    rx_ctxs = (ctx_t*) calloc(rx_thread_num, sizeof(ctx_t));
    addrs = (addr_t*) calloc(rx_thread_num, sizeof(addr_t));
    // Accumulators for compute time for each thread
    compute_time_accs = (time_acc_t *) calloc(tx_thread_num, sizeof(time_acc_t));
    // counters for progress for each progress thread
    progress_counters = (counter_t *) calloc(rx_thread_num, sizeof(counter_t));
    // Accumulators for idle time for each thread
    idle_time_accs = (time_acc_t *) calloc(tx_thread_num, sizeof(time_acc_t));
    // the index of next available workers for each progress thread
    next_worker_idxes = (counter_t *) calloc(rx_thread_num, sizeof(counter_t));

    for (int i = 0; i < rx_thread_num; ++i) {
        init_cq(device, &rx_cqs[i]);
        init_ctx(&device, rx_cqs[i], &rx_ctxs[i], CTX_RX);
        put_ctx_addr(rx_ctxs[i], i);
    }
    for (int i = 0; i < tx_thread_num; ++i) {
        init_cq(device, &tx_cqs[i]);
        init_ctx(&device, tx_cqs[i], &tx_ctxs[i], CTX_TX);
    }
    flush_ctx_addr();
    for (int i = 0; i < rx_thread_num; ++i) {
        get_ctx_addr(device, target_rank, i, &addrs[i]);
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
    while (thread_started.load() != rx_thread_num) continue;

    if (rank < size / 2) {
        omp::thread_run(recv_thread, tx_thread_num);
    } else {
        omp::thread_run(send_thread, tx_thread_num);
    }
    thread_stop = true;
    for (int id = 0; id < rx_thread_num; id++) {
        threads[id].join();
    }

    for (int i = 0; i < tx_thread_num; ++i) {
        free_ctx(&tx_ctxs[i]);
        free_cq(&tx_cqs[i]);
    }
    for (int i = 0; i < rx_thread_num; ++i) {
        free_ctx(&rx_ctxs[i]);
        free_cq(&rx_cqs[i]);
    }
    free_device(&device);
    free(syncs);
    free(tx_cqs);
    free(rx_cqs);
    free(tx_ctxs);
    free(rx_ctxs);
    free(addrs);
    comm_free();
    return 0;
}


