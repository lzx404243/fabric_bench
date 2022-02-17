#pragma once

#include <infiniband/verbs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>
#include <atomic>

namespace fb {

struct device_t {
    ibv_context *dev_ctx;
    ibv_pd *dev_pd;
    uint8_t dev_port;
    ibv_port_attr port_attr;
    ibv_device_attr dev_attr;
    ibv_mr *heap;
    void *heap_ptr;
};

struct cq_t {
    alignas(64) ibv_cq *cq;
    char pad[64 - sizeof(ibv_cq*)];
};

struct srq_t {
    alignas(64) ibv_srq* srq;
    char pad[64 - sizeof(ibv_srq*)];
};

#include "bench_ib_helper.hpp"

struct alignas(64) ctx_t {
    ibv_qp *qp = nullptr;
    conn_info local_conn_info;
    device_t *device = nullptr;
    int pending;
};

struct alignas(64) addr_t {
    conn_info remote_conn_info;
};

struct req_t {
    alignas(64) volatile req_type_t type;// change to atomic
    char pad[64 - sizeof(req_type_t)];
};

struct alignas(64) time_acc_t {
    double tot_time_us = 0;
};

struct alignas(64) counter_t {
    long long progress_count = 0;
};

struct sync_t {
    alignas(64) std::atomic<int> sync;
    char pad[64 - sizeof(std::atomic<int>)];
};

static inline int init_device(device_t *device, bool thread_safe) {
    int num_devices;
    // Get the list of devices
    struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
    if (num_devices <= 0) {
        fprintf(stderr, "Unable to find any ibv devices\n");
        exit(EXIT_FAILURE);
    }

    // Use the last one by default.
    device->dev_ctx = ibv_open_device(dev_list[num_devices - 1]);
    ibv_free_device_list(dev_list);
    int rc = 0;
    // Get device attribute
    rc = ibv_query_device(device->dev_ctx, &device->dev_attr);
    if (rc != 0) {
        fprintf(stderr, "Unable to query device\n");
        exit(EXIT_FAILURE);
    }

    // Find first available port?
    uint8_t dev_port = 0;
    bool is_ib = false;
    for (; dev_port < 128; dev_port++) {
        rc = ibv_query_port(device->dev_ctx, dev_port, &device->port_attr);
        is_ib = device->port_attr.link_layer == IBV_LINK_LAYER_INFINIBAND;
        if (rc == 0 && is_ib) break;
    }

    if (rc != 0 || !is_ib) {
        fprintf(stderr, "Unable to query IB port\n");
        exit(EXIT_FAILURE);
    }
    device->dev_port = dev_port;

    device->dev_pd = ibv_alloc_pd(device->dev_ctx);
    if (device->dev_pd == 0) {
        fprintf(stderr, "Could not create protection domain for context\n");
        exit(EXIT_FAILURE);
    }

    // todo: look at LCI values for memory alignment
    //posix_memalign(&ptr, 8192, HEAP_SIZE + 8192);
    device->heap = ibv_mem_malloc(device, HEAP_SIZE);
    if (device->heap == 0) {
        printf("Error: Unable to create heap\n");
        exit(EXIT_FAILURE);
    }
    device->heap_ptr = device->heap->addr;
    return FB_OK;
}

static inline int free_device(device_t *device) {
    auto ctx = ibv_close_device(device->dev_ctx);
    if (ctx == 0) {
        exit(EXIT_FAILURE);
    }

    return FB_OK;
}

static inline int init_cq(device_t device, cq_t *cq) {
    // Same completion queue for send and recv work queues
    cq->cq = ibv_create_cq(device.dev_ctx, rx_depth + 1, 0, 0, 0);
    if (cq->cq == 0) {
        printf("Error: Unable to create cq\n");
        exit(EXIT_FAILURE);
    }
    return FB_OK;
}

static inline int free_cq(cq_t *cq) {
    IBV_SAFECALL(ibv_destroy_cq(cq->cq));
    return FB_OK;
}

static inline int init_srq(device_t device, srq_t *srq) {
    // Create shared-receive queue, **number here affect performance**.
    struct ibv_srq_init_attr srq_attr;
    srq_attr.srq_context = 0;
    srq_attr.attr.max_wr = 512;
    srq_attr.attr.max_sge = 1;
    srq_attr.attr.srq_limit = 0;
    srq->srq = ibv_create_srq(device.dev_pd, &srq_attr);
    if (srq->srq == 0) {
        fprintf(stderr, "Could not create shared received queue\n");
        exit(EXIT_FAILURE);
    }
    return FB_OK;
}

static inline int init_ctx(device_t *device, cq_t cq, cq_t rx_cq, ctx_t *ctx, srq_t srq) {
    // Create and initialize queue pair
    ctx->qp = qp_create(device, cq, rx_cq, srq);
    ctx->device = device;
    qp_init(ctx->qp, (int) device->dev_port);

    // Save local conn info in ctx
    struct conn_info *local_conn_info = &ctx->local_conn_info;
    local_conn_info->qp_num = ctx->qp->qp_num;
    local_conn_info->lid = device->port_attr.lid;
    return FB_OK;
}

static inline int free_ctx(ctx_t *ctx) {
    IBV_SAFECALL(ibv_destroy_qp(ctx->qp));
    ctx->qp = nullptr;
    // todo: might need to free ctx->local_conn_info
    return FB_OK;
}
static inline int put_ctx_addr(ctx_t ctx, int id) {
    int comm_rank = pmi_get_rank();
    char key[256];
    char value[256];
    struct conn_info *local_info = &ctx.local_conn_info;
    sprintf(key, "_IB_KEY_%d_%d", comm_rank, id);
    sprintf(value, "%d-%d", local_info->qp_num, (int) local_info->lid);
    pmi_put(key, value);
    return FB_OK;
}

static inline int flush_ctx_addr() {
    pmi_barrier();
    return FB_OK;
}

static inline int get_ctx_addr(device_t device, int rank, int id, addr_t *addr) {
    char key[256];
    char value[256];
    struct conn_info remote_info;
    sprintf(key, "_IB_KEY_%d_%d", rank, id);
    pmi_get(key, value);
    sscanf(value, "%d-%d", &remote_info.qp_num, &remote_info.lid);
    // Save the remote connection info in addr
    addr->remote_conn_info = remote_info;

    return FB_OK;
}

static inline void connect_ctx(ctx_t &ctx, addr_t target) {
     qp_to_rtr(ctx.qp, ctx.device->dev_port, &ctx.device->port_attr, &target.remote_conn_info);
     qp_to_rts(ctx.qp);
}

static inline int progress_new(cq_t cq, int* completed_workers) {
    const int numToPoll = 1;
    struct ibv_wc wc[numToPoll];
    int numCompleted;
    //printf("polling cq\n");

    numCompleted = ibv_poll_cq(cq.cq, numToPoll, wc);

    if (numCompleted < 0) {
        printf("Error: ibv_poll_cq() failed\n");
        exit(EXIT_FAILURE);
    }
    // No completion event
    if (numCompleted == 0 || !completed_workers) {
        return 0;
    }

    for (int i = 0; i < numCompleted; i++) {
        if (wc[i].status != ibv_wc_status::IBV_WC_SUCCESS) {
            printf("Error: Failed status %s (%d)\n",
                   ibv_wc_status_str(wc[i].status),
                   wc[i].status);
            exit(EXIT_FAILURE);
        }
        // Success, put the worker ids in the array
        completed_workers[i] = (int)wc[i].wr_id;
    }

    return numCompleted;
}

static inline void isend_tag(ctx_t ctx, void *src, size_t size, int tag, req_t *req) {
    req->type = REQ_TYPE_PEND;
    int send_flags = IBV_SEND_SIGNALED;
    // todo: zli89--remove hardcoded value(max inline size)
    if (size < PERFTEST_MAX_INLINE_SIZE) {
        send_flags |= IBV_SEND_INLINE;
    }
    struct ibv_sge list = {
            .addr = (uintptr_t) src,
            .length = size,
            .lkey = ctx.device->heap->lkey};

    struct ibv_send_wr wr = {
            .wr_id = (uintptr_t)req,
            .sg_list = &list,
            .num_sge = 1,
            .opcode = IBV_WR_SEND,
            .send_flags = send_flags,
    };
    struct ibv_send_wr *bad_wr;

    IBV_SAFECALL(ibv_post_send(ctx.qp, &wr, &bad_wr));
    //printf("Send: posted(rank %d)\n", pmi_get_rank());
    return;
}

// used in pingpong_sym only
static inline void irecv_tag(ctx_t ctx, void *src, size_t size, int tag, req_t *req) {
    //printf("entering - irecv_tag\n");
    //req->type = REQ_TYPE_PEND;

    // if (ctx.qp->state == IBV_QPS_INIT) {
    //     //printf("Setting qp to correct state\n");

    //     // set qp to correct state(rts)
    //     qp_to_rtr(ctx.qp, ctx.device->dev_port, &ctx.device->port_attr, &source.remote_conn_info);
    //     qp_to_rts(ctx.qp);
    // }
    //printf("Recv: ready\n");

    // proceed with normal recv here
    struct ibv_sge list = {
            .addr = (uintptr_t) src,
            .length = size,
            .lkey = ctx.device->heap->lkey};
    struct ibv_recv_wr wr = {
            .wr_id = tag,
            .sg_list = &list,
            .num_sge = 1,
    };
    struct ibv_recv_wr *bad_wr;
    IBV_SAFECALL(ibv_post_recv(ctx.qp, &wr, &bad_wr));
    //printf("Recv: done\n");
    return;
}

static inline void irecv_tag_srq(device_t& device, void *src, size_t size, int tag, srq_t *srq) {
    struct ibv_sge list = {
            .addr = (uintptr_t) src,
            .length = size,
            .lkey = device.heap->lkey};

    struct ibv_recv_wr wr = {
            .wr_id = (uint64_t) tag,
            .sg_list = &list,
            .num_sge = 1,
            };
    struct ibv_recv_wr *bad_wr;
    IBV_SAFECALL(ibv_post_srq_recv(srq->srq, &wr, &bad_wr));
    return;
}


}// namespace fb