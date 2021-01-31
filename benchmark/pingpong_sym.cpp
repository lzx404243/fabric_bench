#include <iostream>
#include "comm_exp.hpp"
#include "bench_ofi.hpp"
#include "bench_omp.hpp"

int thread_num = 1;
int min_size = 8;
int max_size = 64;
bool touch_data = true;
int rank, size, target_rank;
device_t device;
cq_t *cqs;
ctx_t *tx_ctxs;
ctx_t *rx_ctxs;
addr_t *addrs;

void* send_thread(void* arg) {
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    cq_t& cq = cqs[thread_id];
    ctx_t& tx_ctx = tx_ctxs[thread_id];
    ctx_t& rx_ctx = rx_ctxs[thread_id];
    char *buf = (char*) device.heap_ptr + thread_id * max_size;
    char s_data = rank * thread_count + thread_id;
    char r_data = target_rank * thread_count + thread_id;
    req_t req = {REQ_TYPE_NULL};

    omp::thread_barrier();
    double t = wtime();

    RUN_VARY_MSG({min_size, max_size}, true, [&](int msg_size, int iter) {
      if (touch_data) write_buffer(buf, msg_size, s_data);
      isend_tag(tx_ctx, buf, msg_size, addrs[thread_id], thread_id, &req);
      while (req.type != REQ_TYPE_NULL) progress(cq);
      irecv_tag(rx_ctx, buf, msg_size, addrs[thread_id], thread_id, &req);
      while (req.type != REQ_TYPE_NULL) progress(cq);
      if (touch_data) check_buffer(buf, msg_size, r_data);
    }, {thread_id, thread_count});

    return nullptr;
}

void* recv_thread(void* arg) {
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    cq_t& cq = cqs[thread_id];
    ctx_t& tx_ctx = tx_ctxs[thread_id];
    ctx_t& rx_ctx = rx_ctxs[thread_id];
    char *buf = (char*) device.heap_ptr + thread_id * max_size;
    char s_data = rank * thread_count + thread_id;
    char r_data = target_rank * thread_count + thread_id;
    req_t req = {REQ_TYPE_NULL};

    omp::thread_barrier();
    double t = wtime();

    RUN_VARY_MSG({min_size, max_size}, false, [&](int msg_size, int iter) {
      irecv_tag(rx_ctx, buf, msg_size, addrs[thread_id], thread_id, &req);
      while (req.type != REQ_TYPE_NULL) progress(cq);
      if (touch_data) check_buffer(buf, msg_size, r_data);
      if (touch_data) write_buffer(buf, msg_size, s_data);
      isend_tag(tx_ctx, buf, msg_size, addrs[thread_id], thread_id, &req);
      while (req.type != REQ_TYPE_NULL) progress(cq);
    }, {thread_id, thread_count});

    return nullptr;
}

int main(int argc, char *argv[]) {
    if (argc > 1)
        thread_num = atoi(argv[1]);
    if (argc > 2)
        min_size = atoi(argv[2]);
    if (argc > 3)
        max_size = atoi(argv[3]);

    if (thread_num * max_size > HEAP_SIZE){
        printf("HEAP_SIZE is too small! (%d < %d required)\n", HEAP_SIZE, thread_num * max_size);
        exit(1);
    }
    init_device(&device);
    rank = pmi_get_rank();
    size = pmi_get_size();
    target_rank = (rank + size / 2) % size;

    cqs = (cq_t*) calloc(thread_num, sizeof(cq_t));
    tx_ctxs = (ctx_t*) calloc(thread_num, sizeof(ctx_t));
    rx_ctxs = (ctx_t*) calloc(thread_num, sizeof(ctx_t));
    addrs = (addr_t*) calloc(thread_num, sizeof(addr_t));
    for (int i = 0; i < thread_num; ++i) {
        init_cq(device, &cqs[i]);
        init_tx_ctx(device, cqs[i], &tx_ctxs[i]);
        init_rx_ctx(device, cqs[i], &rx_ctxs[i]);
        put_ctx_addr(rx_ctxs[i], i);
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
    return 0;
}


