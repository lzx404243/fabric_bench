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

struct alignas(64) cq_t {
    alignas(64) ibv_cq *cq;
    char pad[64 - sizeof(ibv_cq*)];
};

struct srq_t {
    alignas(64) ibv_srq* srq = nullptr;
    char pad[64 - sizeof(ibv_srq*)];
};

// todo: consider modify placement of the following
#include "bench_ib_helper.hpp"

struct alignas(64) ctx_t {
    ibv_qp *qp = nullptr;
    conn_info local_conn_info;
    device_t *device = nullptr;
    ibv_srq *srq = nullptr;
};

struct alignas(64) addr_t {
    conn_info remote_conn_info;
};

struct req_t {
    alignas(64) volatile req_type_t type;// change to atomic
    char pad[64 - sizeof(req_type_t)];
};

const addr_t ADDR_ANY = {};

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
    const int CQ_LEN = RX_QUEUE_LEN + 1;
    cq->cq = ibv_create_cq(device.dev_ctx, CQ_LEN, 0, 0, 0);
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
    srq_attr.attr.max_wr = RX_QUEUE_LEN;
    srq_attr.attr.max_sge = 1;
    srq_attr.attr.srq_limit = 0;
    srq->srq = ibv_create_srq(device.dev_pd, &srq_attr);
    if (!srq->srq) {
        fprintf(stderr, "Could not create shared received queue\n");
        exit(EXIT_FAILURE);
    }
    return FB_OK;
}

static inline int init_ctx(device_t *device, cq_t send_cq, cq_t recv_cq, srq_t srq, ctx_t *ctx, uint64_t mode) {
    ctx->device = device;
    if (mode == CTX_RX) {
        // receive-only context. Only need to set the share receive queue
        ctx->srq = srq.srq;
        return;
    }
    // Create and initialize queue pair
    ctx->qp = qp_create(device, send_cq, recv_cq, srq);
    qp_init(ctx->qp, (int) device->dev_port);
    // Save local conn info in ctx for later exchange
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

static inline int get_num_ctx_addr(int num_sender, int num_receiver) {
    // only the sender has queue pair that needs to be addressed
    return num_sender;
}

static inline ctx_t* get_exchanged_ctxs(ctx_t* tx_ctxs, ctx_t* rx_ctxs) {
    return tx_ctxs;
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

static inline int progress(cq_t cq) {
    // todo: make the following as config, along with other things else
    const int numToPoll = 16;
    struct ibv_wc wc[numToPoll];
    int numCompleted;
    numCompleted = ibv_poll_cq(cq.cq, numToPoll, wc);

    if (numCompleted < 0) {
        printf("Error: ibv_poll_cq() failed\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < numCompleted; i++) {
        if (wc[i].status != ibv_wc_status::IBV_WC_SUCCESS) {
            printf("Error: Failed status %s (%d)\n",
                   ibv_wc_status_str(wc[i].status),
                   wc[i].status);
            exit(EXIT_FAILURE);
        }
        // todo: the following is used in the symetric case and may be removed
        //  got completed entry from cq. set result type
//        auto* req = (req_t*) wc[i].wr_id;
//        req->type = REQ_TYPE_NULL;
    }
    return numCompleted;
}

static inline void isend(ctx_t ctx, void *src, size_t size, addr_t target, req_t *req) {

    int send_flags = IBV_SEND_SIGNALED;
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
    return;
}

// used in symmetric setup, but not the progress thread one
static inline void irecv_non_srq(ctx_t ctx, void *src, size_t size) {
    struct ibv_sge list = {
            .addr = (uintptr_t) src,
            .length = size,
            .lkey = ctx.device->heap->lkey};
    struct ibv_recv_wr wr = {
            .sg_list = &list,
            .num_sge = 1,
    };
    struct ibv_recv_wr *bad_wr;
    IBV_SAFECALL(ibv_post_recv(ctx.qp, &wr, &bad_wr));
    return;
}

static inline void irecv(ctx_t ctx, void *src, size_t size, addr_t source, int count) {
    // todo: merged non-srq and srq version of code
    if (count == 0) {
        return;
    }
    struct ibv_sge list = {
            .addr = (uintptr_t) src,
            .length = size,
            .lkey = ctx.device->heap->lkey
    };
}

static inline void irecv_srq(ctx_t ctx, void *src, size_t size, addr_t source, int count) {


    // todo: no need to malloc.
    struct ibv_recv_wr* wrs = (struct ibv_recv_wr*) malloc(sizeof(ibv_recv_wr) * count);

    for (int i = 0; i < count; i++) {

        struct ibv_recv_wr wr = {
                .wr_id = (uint64_t) 0,
                .next = &wrs[i + 1],
                .sg_list = &list,
                .num_sge = 1,
                };
        wrs[i] = wr;
    }
    wrs[count - 1].next = NULL;


    struct ibv_recv_wr *bad_wr;
    IBV_SAFECALL(ibv_post_srq_recv(ctx.qp->srq, &wrs[0], &bad_wr));
    return;
}


}// namespace fb