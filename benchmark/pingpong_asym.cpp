#include <iostream>
#include <atomic>
#include "bench_ofi.hpp"
#include "comm_exp.hpp"
#include "thread_utils.hpp"

int tx_thread_num = 3;
int rx_thread_num = 1;
int min_size = 8;
int max_size = 8192;
bool touch_data = false;
int rank, size, target_rank;
device_t device;
cq_t *tx_cqs;
cq_t *rx_cqs;
ctx_t *tx_ctxs;
ctx_t *rx_ctxs;
addr_t *addrs;

void* send_thread(void* arg) {
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();

    if (thread_id < rx_thread_num) {
        // progress (recv) threads
        ctx_t& rx_ctx = rx_ctxs[thread_id];
        char r_data = target_rank;
        req_t *reqs = (req_t*) calloc(tx_thread_num, sizeof(req_t));

        RUN_VARY_MSG({min_size, max_size}, false, [&](int msg_size, int iter) {
            for (int i = thread_id; i < tx_thread_num; i += rx_thread_num) {
                char *buf = (char*) device.heap_ptr + (2 * i + 1) * max_size;
                irecv_tag(rx_ctx, buf, msg_size, ADDR_ANY, 11, &reqs[i]);
            }
            for (int i = thread_id; i < tx_thread_num; i += rx_thread_num) {
                char *buf = (char*) device.heap_ptr + (2 * i + 1) * max_size;
                while (reqs[i].type != REQ_TYPE_NULL) {
                    progress(rx_cqs[thread_id]);
                }
                if (touch_data) check_buffer(buf, msg_size, r_data);
            }
            omp::thread_barrier();
        }, {0, (size / 2) * tx_thread_num});
        free(reqs);
    } else {
        // worker (send) threads
        int tx_thread_id = thread_id - rx_thread_num;
        addr_t& addr = addrs[tx_thread_id % rx_thread_num];
        ctx_t& tx_ctx = tx_ctxs[tx_thread_id];
        char *buf = (char*) device.heap_ptr + tx_thread_id * max_size * 2;
        char s_data = rank;
        req_t req = {REQ_TYPE_NULL};

        RUN_VARY_MSG({min_size, max_size}, (rank == 0 && tx_thread_id == 0), [&](int msg_size, int iter) {
            if (touch_data) write_buffer(buf, msg_size, s_data);
            isend_tag(tx_ctx, buf, msg_size, addr, 11, &req);
            while (req.type != REQ_TYPE_NULL) progress(tx_cqs[tx_thread_id]);
            omp::thread_barrier();
        }, {0, (size / 2) * tx_thread_num});
    }

    return nullptr;
}

void* recv_thread(void* arg) {
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();

    if (thread_id < rx_thread_num) {
        // progress (recv) threads
        ctx_t& rx_ctx = rx_ctxs[thread_id];
        char r_data = target_rank;
        req_t *reqs = (req_t*) calloc(tx_thread_num, sizeof(req_t));

        RUN_VARY_MSG({min_size, max_size}, false, [&](int msg_size, int iter) {
          for (int i = thread_id; i < tx_thread_num; i += rx_thread_num) {
              char *buf = (char*) device.heap_ptr + (2 * i + 1) * max_size;
              irecv_tag(rx_ctx, buf, msg_size, ADDR_ANY, 11, &reqs[i]);
          }
          for (int i = thread_id; i < tx_thread_num; i += rx_thread_num) {
              char *buf = (char*) device.heap_ptr + (2 * i + 1) * max_size;
              while (reqs[i].type != REQ_TYPE_NULL) {
                  progress(rx_cqs[thread_id]);
              }
              if (touch_data) check_buffer(buf, msg_size, r_data);
          }
          omp::thread_barrier();
        }, {0, (size / 2) * tx_thread_num});
        free(reqs);
    } else {
        // worker (send) threads
        int tx_thread_id = thread_id - rx_thread_num;
        addr_t& addr = addrs[tx_thread_id % rx_thread_num];
        ctx_t& tx_ctx = tx_ctxs[tx_thread_id];
        char *buf = (char*) device.heap_ptr + tx_thread_id * 2 * max_size;
        char s_data = rank;
        req_t req = {REQ_TYPE_NULL};

        RUN_VARY_MSG({min_size, max_size}, (rank == 0 && tx_thread_id == 0), [&](int msg_size, int iter) {
          omp::thread_barrier();
          if (touch_data) write_buffer(buf, msg_size, s_data);
          isend_tag(tx_ctx, buf, msg_size, addr, 11, &req);
          while (req.type != REQ_TYPE_NULL) progress(tx_cqs[tx_thread_id]);
        }, {0, (size / 2) * tx_thread_num});
    }

    return nullptr;
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
    if (argc > 5)
        touch_data = atoi(argv[5]);

    if (tx_thread_num * 2 * max_size > HEAP_SIZE){
        printf("HEAP_SIZE is too small! (%d < %d required)\n", HEAP_SIZE, tx_thread_num * 2 * max_size);
        exit(1);
    }
    init_device(&device, true);
    rank = pmi_get_rank();
    size = pmi_get_size();
    target_rank = (rank + size / 2) % size;

    tx_cqs = (cq_t*) calloc(tx_thread_num, sizeof(cq_t));
    rx_cqs = (cq_t*) calloc(rx_thread_num, sizeof(cq_t));
    tx_ctxs = (ctx_t*) calloc(tx_thread_num, sizeof(ctx_t));
    rx_ctxs = (ctx_t*) calloc(rx_thread_num, sizeof(ctx_t));
    addrs = (addr_t*) calloc(rx_thread_num, sizeof(addr_t));
    for (int i = 0; i < rx_thread_num; ++i) {
        init_cq(device, &rx_cqs[i]);
        init_ctx(&device, rx_cqs[i], &rx_ctxs[i], CTX_RECV);
        put_ctx_addr(rx_ctxs[i], i);
    }
    for (int i = 0; i < tx_thread_num; ++i) {
        init_cq(device, &tx_cqs[i]);
        init_ctx(&device, tx_cqs[i], &tx_ctxs[i], CTX_SEND);
    }
    flush_ctx_addr();
    for (int i = 0; i < rx_thread_num; ++i) {
        get_ctx_addr(device, target_rank, i, &addrs[i]);
    }

    if (rank < size / 2) {
        omp::thread_run(send_thread, tx_thread_num + rx_thread_num);
    } else {
        omp::thread_run(recv_thread, tx_thread_num + rx_thread_num);
    }
    return 0;
}


