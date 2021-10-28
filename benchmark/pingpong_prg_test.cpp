#include "bench_fabric.hpp"
#include "comm_exp.hpp"
#include "thread_utils.hpp"
#include "bench_ib.hpp"
#include <atomic>
#include <vector>
#include <unordered_map>
#include <thread>
#include <boost/tokenizer.hpp>

using namespace boost;
#define _GNU_SOURCE // sched_getcpu(3) is glibc-specific (see the man page)

#include <sched.h>
using namespace fb;

int thread_num = 4;
int min_size = 8;
int max_size = 64 * 1024;
bool touch_data = false;
int rank, size, target_rank;
device_t device;
cq_t *cqs;
cq_t *rx_cqs;
ctx_t *ctxs;
addr_t *addrs;
srq_t * srqs;

struct sync_t {
    alignas(64) volatile std::atomic<int> sync;
    char pad[64 - sizeof(std::atomic<int>)];
};

sync_t *syncs;
std::atomic<bool> thread_stop = {false};
std::atomic<int> thread_started = {0};
int rx_thread_num = 1;

std::vector<int> prg_thread_bindings;

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
//     printf("I am %d, sending msg. iter first is %d, iter second is %d\n", rank,
//            (rank % (size / 2) * thread_count + thread_id),
//            ((size / 2) * thread_count));
    int count = 0;
    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        isend_tag(ctx, s_buf, msg_size, thread_id, &req);
        while (req.type != REQ_TYPE_NULL) {
            //  progress for send completion
            auto send_reqs = progress_new(cq);
            if (send_reqs.size() > 1) {
                printf("more than one send is progressed\n");
                exit(EXIT_FAILURE);
            }
            for (int i = 0; i < send_reqs.size(); i++) {
                send_reqs[i]->type = REQ_TYPE_NULL;
            }
        }
        while (syncs[thread_id].sync == 0) continue;
        --syncs[thread_id].sync;
        }, {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});

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
    fprintf(stderr, "Thread %3d is running on CPU %3d\n", thread_id, cpu_num);

//     printf("I am %d, recving msg. iter first is %d, iter second is %d\n", rank,
//            (rank % (size / 2) * thread_count + thread_id),
//            ((size / 2) * thread_count));

    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        while (syncs[thread_id].sync == 0) continue;
        --syncs[thread_id].sync;
        isend_tag(ctx, s_buf, msg_size, thread_id, &req);
        while (req.type != REQ_TYPE_NULL) {
            //  progress for send completion
            auto send_reqs = progress_new(cq);
            if (send_reqs.size() > 1) {
                printf("more than one send is progressed\n");
                exit(EXIT_FAILURE);
            }
            for (int i = 0; i < send_reqs.size(); i++) {
                send_reqs[i]->type = REQ_TYPE_NULL;
            }
        }
        }, {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});

    return nullptr;
}

void progress_thread(int id) {
    int spin = 64;
    bool bind_prg_thread = true;
    // Each worker thread is receiving a fix number of messages
    std::vector<int> thread_recv_count(thread_num);
    std::unordered_map<req_t*, int> reqToWorkerNum;
    auto total_messages = TOTAL;// + SKIP;
    auto msg_count = total_messages / thread_num;
    auto remainder = total_messages % thread_num;
    //printf("Each thread takes %d messages. remainder: %d\n", msg_count, remainder);
    for (int i = 0; i < thread_num; i++) {
        thread_recv_count[i] = (i < remainder) ? msg_count + 1 : msg_count;
    }

    if (bind_prg_thread) {
        // bind progress thread to core using the input binding specification if available
        auto err = comm_set_me_to(!prg_thread_bindings.empty() ? prg_thread_bindings[id] : 15);
        if (err) {
            errno = err;
            printf("setting progress thread affinity failed: error %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    // Put the queue pairs to the correct states
    for (int i = id; i < thread_num; i += rx_thread_num) {
        connect_ctx(ctxs[i], addrs[i]);
    }
    int cpu_num = sched_getcpu();
    fprintf(stderr, "progress Thread is running on CPU %3d\n",  cpu_num);
    req_t *reqs = (req_t*) calloc(thread_num, sizeof(req_t));
    // Construct request-thread_num mapping for fast lookup
    for (int i = id; i < thread_num; i += rx_thread_num) {
        reqToWorkerNum[&reqs[i]] = i;
    }
    // Post receive requests
    for (int i = id; i < thread_num; i += rx_thread_num) {
        char *buf = (char*) device.heap_ptr + (2 * i + 1) * max_size;
        irecv_tag_srq(device, buf, max_size, i, &reqs[i], &srqs[id]);
    }
    // Mark the thread as started
    thread_started++;
    while (!thread_stop.load()) {
        // Progress the receives
        auto recv_reqs = progress_new(rx_cqs[id]);
        for (int i = 0; i < recv_reqs.size(); i++) {
            // zli89: when the progress thread receives certain message
            int worker_num = reqToWorkerNum[recv_reqs[i]];
            ++syncs[worker_num].sync;
            // When each worker thread receives enough, don't post receive for this thread
            if (--thread_recv_count[worker_num] > 0) {
                char *buf = (char*) device.heap_ptr + (2 * worker_num + 1) * max_size;
                irecv_tag_srq(device, buf, max_size, worker_num, &reqs[worker_num], &srqs[id]);
            }
        }
    }
    free(reqs);
}

int main(int argc, char *argv[]) {
    if (argc > 1)
        thread_num = atoi(argv[1]);
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
    //printf("got all arguments");
    if (thread_num * 2 * max_size > HEAP_SIZE) {
        printf("HEAP_SIZE is too small! (%d < %d required)\n", HEAP_SIZE, thread_num * 2 * max_size);
        exit(1);
    }
    comm_init();
    init_device(&device, thread_num != 1);

    rank = pmi_get_rank();
    size = pmi_get_size();
    target_rank = (rank + size / 2) % size;
    // Send completion queues(per workers)
    cqs = (cq_t *) calloc(thread_num, sizeof(cq_t));
    // Send context (per workers)
    ctxs = (ctx_t *) calloc(thread_num, sizeof(ctx_t));
    addrs = (addr_t *) calloc(thread_num, sizeof(addr_t));
    // Receive completion queues(per progress thread)
    rx_cqs = (cq_t*) calloc(rx_thread_num, sizeof(cq_t));
    // Shared receive queues queues(per progress thread)
    srqs = (srq_t*) calloc(rx_thread_num, sizeof(srq_t));
    // Set up receive completion queue, one per progress thread
    for (int i = 0; i < rx_thread_num; ++i) {
        init_cq(device, &rx_cqs[i]);
    }
    for (int i = 0; i < rx_thread_num; ++i) {
        init_srq(device, &srqs[i]);
    }
    for (int i = 0; i < thread_num; ++i) {
        init_cq(device, &cqs[i]);
        init_ctx(&device, cqs[i], rx_cqs[i % rx_thread_num], &ctxs[i], srqs[i % rx_thread_num]);
        put_ctx_addr(ctxs[i], i);
    }

    flush_ctx_addr();
    for (int i = 0; i < thread_num; ++i) {
        get_ctx_addr(device, target_rank, i, &addrs[i]);
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
    // todo: fix resource cleanup error
    for (int i = 0; i < thread_num; ++i) {
        free_ctx(&ctxs[i]);
        free_cq(&cqs[i]);
    }
    free_device(&device);
    free(addrs);
    free(ctxs);
    free(cqs);
    free(rx_cqs);
    comm_free();
    return 0;
}
