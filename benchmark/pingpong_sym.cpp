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
cq_t *cqs;
ctx_t *ctxs;
addr_t *addrs;
int routs = 0;

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
                            irecv_tag(*ctx, buf, msg_size, 0, nullptr);
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
    cq_t &cq = cqs[thread_id];
    ctx_t &ctx = ctxs[thread_id];
    // todo: zheli -- the following is a hack to specify the thread as a sending thread
    ctx.mode = CTX_TX;
    char *buf = (char *) device.heap_ptr + thread_id * max_size;
    char s_data = rank * thread_count + thread_id;
    char r_data = target_rank * thread_count + thread_id;
    req_t req = {REQ_TYPE_NULL};
    // printf("I am %d, sending msg. iter first is %d, iter second is %d\n", rank,
    //        (rank % (size / 2) * thread_count + thread_id),
    //        ((size / 2) * thread_count));
    //printf("Setting qp to correct state");
    connect_ctx(ctx, addrs[thread_id]);

    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        for (int i = 0; i < fb::rx_depth; i++) {
            irecv_tag(ctx, buf, msg_size, thread_id, &req);
            routs++;
        }
        // This side also send the first message
        isend_tag(ctx, buf, msg_size, thread_id, &req);
        ctx.pending = PINGPONG_RECV_WRID | PINGPONG_SEND_WRID;

        run_pingpong(msg_size, iter, &ctx, cq, buf, addrs[thread_id]);
    },
                 {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});

    return nullptr;
}


void *recv_thread(void *arg) {
    //printf("I am a recv thread\n");
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    cq_t &cq = cqs[thread_id];
    ctx_t &ctx = ctxs[thread_id];
    ctx.mode = CTX_RX;

    char *buf = (char *) device.heap_ptr + thread_id * max_size;
    char s_data = rank * thread_count + thread_id;
    char r_data = target_rank * thread_count + thread_id;
    req_t req = {REQ_TYPE_NULL};

    // printf("I am %d, recving msg. iter first is %d, iter second is %d\n", rank,
    //        (rank % (size / 2) * thread_count + thread_id),
    //        ((size / 2) * thread_count));
    //printf("Setting qp to correct state");
    connect_ctx(ctx, addrs[thread_id]);

    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter) {
        for (int i = 0; i < fb::rx_depth; i++) {
            irecv_tag(ctx, buf, msg_size, 0, nullptr);
            routs++;
        }
        ctx.pending = PINGPONG_RECV_WRID;
        run_pingpong(msg_size, iter, &ctx, cq, buf, addrs[thread_id]);
    },
                 {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});

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
    //printf("calling comm_init\n");
    comm_init();
    //printf("comm inited\n");

    init_device(&device, thread_num != 1);
    //printf("device inited\n");

    rank = pmi_get_rank();
    size = pmi_get_size();
    target_rank = (rank + size / 2) % size;

    cqs = (cq_t *) calloc(thread_num, sizeof(cq_t));
    ctxs = (ctx_t *) calloc(thread_num, sizeof(ctx_t));
    addrs = (addr_t *) calloc(thread_num, sizeof(addr_t));
    //printf("init per thread structures\n");
    for (int i = 0; i < thread_num; ++i) {
        init_cq(device, &cqs[i]);
        init_ctx(&device, cqs[i], &ctxs[i], CTX_TX | CTX_RX);
        put_ctx_addr(ctxs[i], i);
    }
    flush_ctx_addr();
    for (int i = 0; i < thread_num; ++i) {
        get_ctx_addr(device, target_rank, i, &addrs[i]);
    }
    
    if (rank < size / 2) {
        omp::thread_run(send_thread, thread_num);
    } else {
        omp::thread_run(recv_thread, thread_num);
    }

    //printf("Done: Freeing resources\n");
    for (int i = 0; i < thread_num; ++i) {
        free_ctx(&ctxs[i]);
        free_cq(&cqs[i]);
    }
    free_device(&device);
    free(addrs);
    free(ctxs);
    free(cqs);
    comm_free();
    return 0;
}
