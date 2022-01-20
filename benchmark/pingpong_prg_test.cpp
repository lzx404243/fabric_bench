#include "bench_fabric.hpp"
#include "comm_exp.hpp"
#include "thread_utils.hpp"
#include "bench_ib.hpp"
#include <vector>
#include <unordered_map>
#include <thread>
#include <boost/tokenizer.hpp>
#include <chrono>
#include <math.h>

using namespace boost;
#define _GNU_SOURCE // sched_getcpu(3) is glibc-specific (see the man page)

#include <sched.h>
using namespace fb;

int thread_num = 4;
int min_size = 8;
int max_size = 1024;
bool touch_data = false;
int rank, size, target_rank;
device_t device;
cq_t *cqs;
cq_t *rx_cqs;
ctx_t *ctxs;
addr_t *addrs;
srq_t * srqs;



sync_t *syncs;
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
int num_qp = thread_num;

void *send_thread(void *arg) {
    //printf("I am a send thread\n");
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    int cpu_num = sched_getcpu();
    fprintf(stderr, "Thread %3d is running on CPU %3d\n", thread_id, cpu_num);
    ctx_t &ctx = ctxs[thread_id];
    char *s_buf = (char *) device.heap_ptr + thread_id * 2 * max_size;
    auto& cq = cqs[thread_id];
    req_t req = {REQ_TYPE_NULL};
    time_acc_t &time_acc = compute_time_accs[thread_id];
    time_acc_t &idle_time_acc = idle_time_accs[thread_id];
    bool idled = false;
    struct timespec start, stop;
//     printf("I am %d, sending msg. iter first is %d, iter second is %d\n", rank,
//            (rank % (size / 2) * thread_count + thread_id),
//            ((size / 2) * thread_count));
    int count = 0;
    RUN_VARY_MSG({min_size, min_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        isend_tag(ctx, s_buf, msg_size, thread_id, &req);
        // progress for send completion. Note that we don't block  for the completion of the send here
        progress_new(cq, nullptr);

        while (syncs[thread_id].sync == 0) {
            // idle
            if (!idled) {
                // start timer
                idled = true;
                clock_gettime(CLOCK_REALTIME, &start);
            }
            continue;
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
            //std::this_thread::sleep_for(std::chrono::milliseconds(compute_time_in_us));
        }
        }, {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});

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
    ctx_t &ctx = ctxs[thread_id];
    char *s_buf = (char *) device.heap_ptr + thread_id * 2 * max_size;
    req_t req = {REQ_TYPE_NULL};
    int cpu_num = sched_getcpu();
    auto& cq = cqs[thread_id];
    time_acc_t &time_acc = compute_time_accs[thread_id];
    time_acc_t &idle_time_acc = idle_time_accs[thread_id];
    bool idled = false;
    struct timespec start, stop;

    fprintf(stderr, "Thread %3d is running on CPU %3d\n", thread_id, cpu_num);

//     printf("I am %d, recving msg. iter first is %d, iter second is %d\n", rank,
//            (rank % (size / 2) * thread_count + thread_id),
//            ((size / 2) * thread_count));

RUN_VARY_MSG({min_size, min_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        while (syncs[thread_id].sync == 0) {
            // idle
            if (!idled) {
                // start timer
                idled = true;
                clock_gettime(CLOCK_REALTIME, &start);
            }
            continue;
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
            //std::this_thread::sleep_for(std::chrono::milliseconds(compute_time_in_us));
        }
        isend_tag(ctx, s_buf, msg_size, thread_id, &req);
        // progress for send completion. Note that we don't block for the completion of the send here
        progress_new(cq, nullptr);

        }, {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});

    // check whether the thread has migrated
    int cpu_num_final = sched_getcpu();
    if (cpu_num_final != cpu_num) {
        fprintf(stderr, "Thread %3d migrated to %3d\n", thread_id, cpu_num_final);
        exit(2);
    }
    return nullptr;
}

void progress_loop(int id, int iter) {
    // real iteration
    // Each worker thread is receiving a fixed number of messages
    std::vector<int> thread_recv_count(thread_num);
    long long& progress_counter = progress_counters[id].progress_count;
    progress_counter = 0;
    auto msg_count = iter / thread_num;
    auto remainder = iter % thread_num;
    auto finished_worker = 0;
    for (int i = 0; i < thread_num; i++) {
        thread_recv_count[i] = (i < remainder) ? msg_count + 1 : msg_count;
    }
    // Post receive requests
    for (int i = id; i < thread_num; i += rx_thread_num) {
        char *buf = (char*) device.heap_ptr + (2 * i + 1) * max_size;
        irecv_tag_srq(device, buf, max_size, i, &srqs[id]);
    }
    // list of worker ids that's polled from the completion queue as requests are completed
    int* completed_worker_num_from_cq = (int *) calloc(thread_num, sizeof(int));
    int numWorkersCompleted;
    int backoff_in_ns;
    // Mark the thread as started
    thread_started++;
    while (!thread_stop.load()) {
        // Progress the receives
        numWorkersCompleted = 0;
        //backoff_in_ns = 100;
        while (1) {
            numWorkersCompleted = progress_new(rx_cqs[id], completed_worker_num_from_cq);
            if (numWorkersCompleted > 0) {
                break;
            }
            progress_counter++;
//            backoff_in_ns *= 2;
//            sleep_for_ns(backoff_in_ns);
        }

        // got something from the cq
        //for (int idx = 0; idx < numWorkersCompleted; idx++) {
        int idx = 0;
        // zli89: when the progress thread receives certain message
        int worker_num = completed_worker_num_from_cq[idx];
        ++syncs[worker_num].sync;
        // When each worker thread receives enough, don't post receive for this thread
        if (--thread_recv_count[worker_num] > 0) {
            char *buf = (char *) device.heap_ptr + (2 * worker_num + 1) * max_size;
            irecv_tag_srq(device, buf, max_size, worker_num, &srqs[id]);
        } else {
            // this worker is done
            // one worker is done, phasing out.
            if (finished_worker == 0) {
                thread_started--;
            }
            // todo: this is added for support for SKIP. With this the while loop can be ended in two ways.. Also will break if
            // thread_num / rx_thread_num is not an integer
            // Find a better way to signal stop for the progress thread
            if (++finished_worker == (thread_num / rx_thread_num)) {
                break;
            }
        }
        //}
    }
}
void progress_thread(int id) {
    bool bind_prg_thread = true;
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
    progress_loop(id, SKIP);
    progress_loop(id, TOTAL);
}

int main(int argc, char *argv[]) {
    if (argc > 1)
        thread_num = atoi(argv[1]);
    if (argc > 2)
        rx_thread_num = atoi(argv[2]);
    if (argc > 3)
        min_size = atoi(argv[3]);
    if (argc > 4) {
        // todo: fix max_size
        auto tmp_size = atoi(argv[4]);
        if (tmp_size != 8) {
            fprintf(stderr, "max size needs to be fix before size other than 8 can be run.\n");
            exit(EXIT_FAILURE);
        }
    }
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
    if (argc > 8) {
        num_worker_per_qp = atoi(argv[8]);
        if (num_worker_per_qp > floor(thread_num / rx_thread_num)) {
            printf("Number of worker sharing one queue pair is too large. max is: %d\n", floor(thread_num / rx_thread_num));
            exit(EXIT_FAILURE);
        }
    }
    // check worker thread distribution
    if (thread_num % rx_thread_num != 0) {
        printf("number of worker needs to be divisible by number of progress thread. If this is not true, need to change the core binding before running");
        exit(EXIT_FAILURE);
    }
    if (thread_num * 2 * max_size > HEAP_SIZE) {
        printf("HEAP_SIZE is too small! (%d < %d required)\n", HEAP_SIZE, thread_num * 2 * max_size);
        exit(1);
    }
    comm_init();
    init_device(&device, thread_num != 1);

    rank = pmi_get_rank();
    size = pmi_get_size();
    target_rank = (rank + size / 2) % size;
    // Send completion queues (per worker)
    //num_qp = ceil((double)thread_num / num_worker_per_qp);
    cqs = (cq_t *) calloc(thread_num, sizeof(cq_t));
    // Send context (per worker)
    ctxs = (ctx_t *) calloc(thread_num, sizeof(ctx_t));
    addrs = (addr_t *) calloc(thread_num, sizeof(addr_t));
    // Receive completion queues(per progress thread)
    rx_cqs = (cq_t*) calloc(rx_thread_num, sizeof(cq_t));
    // Shared receive queues queues(per progress thread)
    srqs = (srq_t*) calloc(rx_thread_num, sizeof(srq_t));
    // Accumulators for compute time for each thread
    compute_time_accs = (time_acc_t *) calloc(thread_num, sizeof(time_acc_t));
    // counters for progress for each progress thread
    progress_counters = (counter_t *) calloc(thread_num, sizeof(counter_t));
    // Accumulators for idle time for each thread
    idle_time_accs = (time_acc_t *) calloc(thread_num, sizeof(time_acc_t));
    // Set up receive completion queue, one per progress thread
    for (int i = 0; i < rx_thread_num; ++i) {
        init_cq(device, &rx_cqs[i]);
    }
    for (int i = 0; i < rx_thread_num; ++i) {
        init_srq(device, &srqs[i]);
    }
    // set up queue pairs and context
    for (int i = 0; i < rx_thread_num; ++i) {
        int groupCntIdx = 0;
        for (int workerThreadIdx = i; workerThreadIdx < thread_num; workerThreadIdx +=rx_thread_num) {
            // workers handled by the same progress thread
            if (groupCntIdx++ % num_worker_per_qp == 0) {
                // only one in a group creates the queue pair
                //std::cout << "worker " << workerThreadIdx << "is creating resources " << std::endl ;

                init_cq(device, &cqs[workerThreadIdx]);
                init_ctx(&device, cqs[workerThreadIdx], rx_cqs[workerThreadIdx % rx_thread_num], &ctxs[workerThreadIdx], srqs[workerThreadIdx % rx_thread_num]);
            }
            put_ctx_addr(ctxs[workerThreadIdx], workerThreadIdx);
        }
    }
    flush_ctx_addr();
    for (int i = 0; i < thread_num; ++i) {
        get_ctx_addr(device, target_rank, i, &addrs[i]);
    }
    // Put the queue pairs to the correct states
    for (int i = 0; i < thread_num; i++) {
        if (ctxs[i].qp) {
            connect_ctx(ctxs[i], addrs[i]);
        } else {
            // copy shared queue pair and associated cqs from worker in the same group
            //std::cout << "worker " << i << "is coping from " << (i - rx_thread_num) << std::endl ;
            ctxs[i] = ctxs[i - rx_thread_num];
            cqs[i] = cqs[i - rx_thread_num];
        }
    }

    // atomic flag(per worker) used by the progress thread to signal that the worker can read from the receive buf
    syncs = (sync_t*) calloc(thread_num, sizeof(sync_t));
    for (int i = 0; i < thread_num; ++i) {
        syncs[i].sync = 0;
    }
    std::vector<std::thread> threads(rx_thread_num);
    for (int id = 0; id < rx_thread_num; id++) {
        threads[id] = std::thread(progress_thread, id);
    }
    while (thread_started.load() != rx_thread_num) continue;
    //printf("all progress thread ready, starting worker\n");
    if (rank < size / 2) {
        omp::thread_run(send_thread, thread_num);
    } else {
        omp::thread_run(recv_thread, thread_num);
    }
    thread_stop = true;
    for (int id = 0; id < rx_thread_num; id++) {
        threads[id].join();
    }

    // clean up resources
    for (int i = 0; i < rx_thread_num; ++i) {
        int groupCntIdx = 0;
        for (int workerThreadIdx = i; workerThreadIdx < thread_num; workerThreadIdx +=rx_thread_num) {
            // workers handled by the same progress thread
            if (groupCntIdx++ % num_worker_per_qp == 0) {
                free_ctx(&ctxs[workerThreadIdx]);
                free_cq(&cqs[workerThreadIdx]);
            }
        }
    }

    free_device(&device);
    free(addrs);
    free(ctxs);
    free(cqs);
    free(rx_cqs);
    comm_free();
    return 0;
}
