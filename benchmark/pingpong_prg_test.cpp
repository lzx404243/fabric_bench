#include "bench_fabric.hpp"
#include "comm_exp.hpp"
#include "thread_utils.hpp"
#include "bench_ib.hpp"
#include <atomic>
#include <vector>
#include <thread>

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

std::atomic<int> *syncs; // TODO： fix false sharing
std::atomic<bool> thread_stop = {false};
std::atomic<int> thread_started = {0};

int routs = 0;
// todo: currently progress thread number is set to one
constexpr int rx_thread_num = 1;

static void run_pingpong(int msg_size, int iters, ctx_t *ctx, cq_t &cq, char * buf, addr_t addr) {
    //printf("Entering run_pingpong\n");
    int rcnt = 0;
    int scnt = 0;
    while (rcnt < iters || scnt < iters) {
        {
            struct ibv_wc wc[2];
            int ne, i;
            do {
                ne = ibv_poll_cq(cq.cq, 2, wc);
                if (ne < 0) {
                    fprintf(stderr, "poll CQ failed %d\n", ne);
                    exit(EXIT_FAILURE);
                }

            } while (ne < 1);
            //printf("I am %d, Done polling\n", rank);

            for (i = 0; i < ne; ++i) {
                if (wc[i].status != IBV_WC_SUCCESS) {
                    fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
                            ibv_wc_status_str(wc[i].status),
                            wc[i].status, (int) wc[i].wr_id);
                    exit(EXIT_FAILURE);
                }

                switch ((int) wc[i].wr_id) {
                    case PINGPONG_SEND_WRID:
                        //printf("I am %d, completed one send\n", rank);
                        ++scnt;
                        break;

                        case PINGPONG_RECV_WRID:
                            //printf("I am %d, completed one recv\n", rank);
                            ++rcnt;
                            // zli89: don't post recv when we got enough messages
                            if ((rcnt + fb::rx_depth) <= iters) {
                                irecv_tag_srq(device, buf, msg_size, 0, nullptr);
                            }
                            // int additional_recv_cnt = fb::rx_depth - routs;
                            // for (int i = 0; i < additional_recv_cnt; i++) {
                            //     if ((rcnt + routs) == iters) {
                            //         break;
                            //     }
                            //     irecv_tag(*ctx, buf, msg_size, addr, 0, nullptr);
                            //     routs++;
                            // }
                            //printf("I am %d, done posting recv\n", rank);
                            break;

                            default:
                                fprintf(stderr, "Completion for unknown wr_id %d\n",
                                        (int) wc[i].wr_id);
                                exit(EXIT_FAILURE);

                }
                //printf("Unsetting pending - %d \n", rank);

                ctx->pending &= ~(int) wc[i].wr_id;
                //printf("Done Unsetting pending - %d \n", rank);

                if (scnt < iters && !ctx->pending) {
                    // if (pp_post_send(ctx)) {
                    //     fprintf(stderr, "Couldn't post send\n");
                    //     exit(EXIT_FAILURE);
                    // }
                    //printf("I am %d, posting send\n", rank);
                    isend_tag(*ctx, buf, msg_size, 0, nullptr);
                    //printf("I am %d, done posting send\n", rank);

                    ctx->pending = PINGPONG_RECV_WRID |
                            PINGPONG_SEND_WRID;
                }
            }
        }
    }
}

void *send_thread(void *arg) {
    //printf("I am a send thread\n");
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    int cpu_num = sched_getcpu();
    fprintf(stderr, "Thread %3d is running on CPU %3d\n", thread_id, cpu_num);
    ctx_t &ctx = ctxs[thread_id];
    char *s_buf = (char *) device.heap_ptr + thread_id * 2 * max_size;
    req_t req = {REQ_TYPE_NULL};
     printf("I am %d, sending msg. iter first is %d, iter second is %d\n", rank,
            (rank % (size / 2) * thread_count + thread_id),
            ((size / 2) * thread_count));

    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        isend_tag(ctx, s_buf, msg_size, thread_id, &req);
        // todo: currently no polling for send. Think about it(also in recv thread)
        while (syncs[thread_id] == 0) continue;
        syncs[thread_id] = 0;
        printf("worker thread got msg from progress thread!\n");
        }, {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});

//    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
//        for (int i = 0; i < fb::rx_depth; i++) {
//            irecv_tag(ctx, buf, msg_size, addrs[thread_id], thread_id, &req);
//            routs++;
//        }
//        // This side also send the first message
//        isend_tag(ctx, buf, msg_size, addrs[thread_id], thread_id, &req);
//        ctx.pending = PINGPONG_RECV_WRID | PINGPONG_SEND_WRID;
//
//        run_pingpong(msg_size, iter, &ctx, cq, buf, addrs[thread_id]);
//        },
//                 {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});

    return nullptr;
}

void *recv_thread(void *arg) {
    //printf("I am a recv thread\n");
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    ctx_t &ctx = ctxs[thread_id];
    char *s_buf = (char *) device.heap_ptr + thread_id * 2 * max_size;
    req_t req = {REQ_TYPE_NULL};

     printf("I am %d, recving msg. iter first is %d, iter second is %d\n", rank,
            (rank % (size / 2) * thread_count + thread_id),
            ((size / 2) * thread_count));

    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        while (syncs[thread_id] == 0) continue;
        syncs[thread_id] = 0;
        isend_tag(ctx, s_buf, msg_size, thread_id, &req);
        }, {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});
//    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
//        for (int i = 0; i < fb::rx_depth; i++) {
//            irecv_tag(ctx, buf, msg_size, addrs[thread_id], 0, nullptr);
//            routs++;
//        }
//        ctx.pending = PINGPONG_RECV_WRID;
//        run_pingpong(msg_size, iter, &ctx, cq, buf, addrs[thread_id]);
//
//        },
//                 {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});

    return nullptr;
}

void progress_thread(int id) {
    int spin = 64;
    int core = 0;
    // todo: progress thread binding
//    if (getenv("FB_SCORE"))
//        core = atoi(getenv("FB_SCORE"));
//    if (bind_prg_thread)
//        comm_set_me_to(core + 2*id); // only for hyper-threaded. FIXME.

    // Put the queue pairs to the correct states
    for (int i = id; i < thread_num; i += rx_thread_num) {
        connect_ctx(ctxs[i], addrs[i]);
    }
    printf("connected all qps in prg\n");
    req_t *reqs = (req_t*) calloc(thread_num, sizeof(req_t));
    // Post receive requests
    for (int i = id; i < thread_num; i += rx_thread_num) {
        char *buf = (char*) device.heap_ptr + (2 * i + 1) * max_size;
        irecv_tag_srq(device, buf, max_size, i, &reqs[i]);
    }
    // Mark the thread as started
    thread_started++;
    while (!thread_stop.load()) {
        // progress each send completion -- currently skipped
//        for (int j = id; j < thread_num; j += rx_thread_num)
//            progress(cqs[j]);
        // Progress the receives
        bool ret = progress(rx_cqs[id], reqs);
        if (ret) {
            for (int i = id; i < thread_num; i += rx_thread_num) {
                // zli89: when the progress thread receives certain message
                if (reqs[i].type == REQ_TYPE_NULL) {
                    syncs[i] = 1;
                    char *buf = (char*) device.heap_ptr + (2 * i + 1) * max_size;
                    irecv_tag_srq(device, buf, max_size, i, &reqs[i]);
                    break;
                }
            }
        }
        if (spin-- == 0) { sched_yield(); spin = 64; }
    }
    free(reqs);
}

int main(int argc, char *argv[]) {
    // todo: input args might need to change
    if (argc > 1)
        thread_num = atoi(argv[1]);
    if (argc > 2)
        min_size = atoi(argv[2]);
    if (argc > 3)
        max_size = atoi(argv[3]);
    //printf("got all arguments");
    if (thread_num * 2 * max_size > HEAP_SIZE) {
        printf("HEAP_SIZE is too small! (%d < %d required)\n", HEAP_SIZE, thread_num * 2 * max_size);
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

    // Send completion queues(per workers)
    cqs = (cq_t *) calloc(thread_num, sizeof(cq_t));
    // Send context (per workers)
    ctxs = (ctx_t *) calloc(thread_num, sizeof(ctx_t));
    addrs = (addr_t *) calloc(thread_num, sizeof(addr_t));
    // Receive completion queues(per workers)
    rx_cqs = (cq_t*) calloc(rx_thread_num, sizeof(cq_t));
    //printf("init per thread structures\n");
    // Set up receive completion queue, one per progress thread
    for (int i = 0; i < rx_thread_num; ++i) {
        init_cq(device, &rx_cqs[i]);
    }
    for (int i = 0; i < thread_num; ++i) {
        init_cq(device, &cqs[i]);
        init_ctx(&device, cqs[i], rx_cqs[i % rx_thread_num], &ctxs[i]);
        put_ctx_addr(ctxs[i], i);
    }
    flush_ctx_addr();
    for (int i = 0; i < thread_num; ++i) {
        get_ctx_addr(device, target_rank, i, &addrs[i]);
    }

    // atomic flag(per worker) used by the progress thread to signal that the worker can read from the receive buf
    syncs = (std::atomic<int>*) calloc(thread_num, sizeof(std::atomic<int>));
    for (int i = 0; i < thread_num; ++i) {
        syncs[i] = 0;
    }
    // todo: ask Jiakun why this is not using OpenMP)
    std::vector<std::thread> threads(rx_thread_num);
    for (int id = 0; id < rx_thread_num; id++) {
        threads[id] = std::thread(progress_thread, id);
    }
    while (thread_started.load() != rx_thread_num) continue;
    printf("all progress thread ready, starting worker\n");
    if (rank < size / 2) {
        omp::thread_run(send_thread, thread_num);
    } else {
        omp::thread_run(recv_thread, thread_num);
    }
    thread_stop = true;
    for (int id = 0; id < rx_thread_num; id++) {
        threads[id].join();
    }
    //printf("Done: Freeing resources\n");
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
