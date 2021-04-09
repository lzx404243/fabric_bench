#include "bench_fabric.hpp"
#include "comm_exp.hpp"
#include "thread_utils.hpp"
// todo: remove this include before compiling. For now this include is meant for work around with a syntax highlighting issue

#include "bench_ib.hpp"

using namespace fb;

int thread_num = 4;
int min_size = 8;
int max_size = 262144;
bool touch_data = false;
int rank, size, target_rank;
device_t device;
cq_t *cqs;
ctx_t *ctxs;
addr_t *addrs;

void *send_thread(void *arg) {
    printf("I am a send thread\n");
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    cq_t &cq = cqs[thread_id];
    ctx_t &ctx = ctxs[thread_id];
    // todo: zheli -- the following is a hack to specify the thread as a sending thread
    ctx.mode = CTX_TX;
    char *buf = (char *) device.heap_ptr + thread_id * max_size;
    char s_data = rank * thread_count + thread_id;
    char r_data = target_rank * thread_count + thread_id;
    req_t req = {REQ_TYPE_NULL};
    printf("I am %d, sending msg. iter first is %d, iter second is %d\n", rank,
           (rank % (size / 2) * thread_count + thread_id),
           ((size / 2) * thread_count));
    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter, int start = -1, int last = -1) {
        if (touch_data) write_buffer(buf, msg_size, s_data);
        // Post the first receive request before the first send
        irecv_tag(ctx, buf, msg_size, addrs[thread_id], thread_id, &req);
        isend_tag(ctx, buf, msg_size, addrs[thread_id], thread_id, &req);
        // zli89: To test out things, the progress function would be blocking in ib impl.
        // The request type is not used, as only one possible work completion at anytime
        // Todo: find a better way to do this
        progress(cq);
        //irecv_tag(ctx, buf, msg_size, addrs[thread_id], thread_id, &req);
        progress(cq);
        if (touch_data) check_buffer(buf, msg_size, r_data);
    },
                 {rank % (size / 2) * thread_count + thread_id, (size / 2) * thread_count});

    return nullptr;
}

void *recv_thread(void *arg) {
    printf("I am a recv thread\n");
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    cq_t &cq = cqs[thread_id];
    ctx_t &ctx = ctxs[thread_id];
    ctx.mode = CTX_RX;

    char *buf = (char *) device.heap_ptr + thread_id * max_size;
    char s_data = rank * thread_count + thread_id;
    char r_data = target_rank * thread_count + thread_id;
    req_t req = {REQ_TYPE_NULL};

    printf("I am %d, recving msg. iter first is %d, iter second is %d\n", rank,
           (rank % (size / 2) * thread_count + thread_id),
           ((size / 2) * thread_count));

    RUN_VARY_MSG({min_size, max_size}, (rank == 0 && thread_id == 0), [&](int msg_size, int iter, int start = -1, int last = -1) {

        if (iter == start) {
            // Post the first receive request
            irecv_tag(ctx, buf, msg_size, addrs[thread_id], thread_id, &req);
        }
        progress(cq);
        // Immdediately post another receive request when the former one is completed
        if (iter != last) {
            // Don't pre post a recv for last itereration...
            irecv_tag(ctx, buf, msg_size, addrs[thread_id], thread_id, &req);
        }
        if (touch_data) check_buffer(buf, msg_size, r_data);
        if (touch_data) write_buffer(buf, msg_size, s_data);
        isend_tag(ctx, buf, msg_size, addrs[thread_id], thread_id, &req);
        progress(cq);
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
    printf("got all arguments");
    if (thread_num * max_size > HEAP_SIZE) {
        printf("HEAP_SIZE is too small! (%d < %d required)\n", HEAP_SIZE, thread_num * max_size);
        exit(1);
    }
    printf("calling comm_init\n");
    comm_init();
    printf("comm inited\n");

    init_device(&device, thread_num != 1);
    printf("device inited\n");

    rank = pmi_get_rank();
    size = pmi_get_size();
    target_rank = (rank + size / 2) % size;

    cqs = (cq_t *) calloc(thread_num, sizeof(cq_t));
    ctxs = (ctx_t *) calloc(thread_num, sizeof(ctx_t));
    addrs = (addr_t *) calloc(thread_num, sizeof(addr_t));
    printf("init per thread structures\n");
    for (int i = 0; i < thread_num; ++i) {
        init_cq(device, &cqs[i]);
        printf("created cq\n");
        init_ctx(&device, cqs[i], &ctxs[i], CTX_TX | CTX_RX);
        printf("ctx inited\n");
        put_ctx_addr(ctxs[i], i);
        printf("done putting ctx addr\n");
    }
    flush_ctx_addr();
    for (int i = 0; i < thread_num; ++i) {
        get_ctx_addr(device, target_rank, i, &addrs[i]);
    }
    printf("I am rank %d, my target rank is %d, address obtained\n", rank, target_rank);

    if (rank < size / 2) {
        omp::thread_run(send_thread, thread_num);
    } else {
        omp::thread_run(recv_thread, thread_num);
    }

    printf("Done: Freeing resources\n");
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
