#ifndef FABRICBENCH_BENCH_IB_HPP
#define FABRICBENCH_BENCH_IB_HPP

#include <infiniband/verbs.h>
#include <stdio.h>
#include <stdlib.h>
// todo: remove this include before compiling. For now this include is meant for work around with a syntax highlighting issue
#include "bench_fabric.hpp"
#include "bench_ib_helper.hpp"

namespace fb {

#define IBV_SAFECALL(x)                                                     \
    {                                                                       \
        int err = (x);                                                      \
        if (err) {                                                          \
            fprintf(stderr, "err : %d (%s:%d)\n", err, __FILE__, __LINE__); \
            exit(EXIT_FAILURE);                                             \
        }                                                                   \
    }                                                                       \
    while (0)                                                               \
        ;
struct device_t {
    ibv_device *ib_device;
    ibv_context *dev_ctx;
    ibv_pd *dev_pd;
    ibv_srq *dev_srq;
    ibv_qp *dev_qp;
    void *heap_ptr;
    ibv_port_attr *port_attr;
    ibv_mr* heap;
    void *heap_ptr;;
};

struct cq_t {
    ibv_cq *send_cq;
    ibv_cq *recv_cq;
};
struct ctx_t {
    ibv_context *dev_ctx;
    device_t *device;
};
struct addr_t {
};

struct req_t {
    alignas(64) volatile req_type_t type;// change to atomic
    char pad[64 - sizeof(req_type_t)];
};

// todo: check if the following is required
//onst addr_t ADDR_ANY = {FI_ADDR_UNSPEC};


// todo: finish device and ctx
static inline int init_device(device_t *device, bool thread_safe) {
    int num_devices;
    // Get the list of devices
    struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
    if (num_devices <= 0) {
        fprintf(stderr, "Unable to find any ibv devices\n");
        exit(EXIT_FAILURE);
    }
    // Use the last one by default.
    device->ib_device = dev_list[num_devices - 1];

    ibv_free_device_list(dev_list);

    // Get device attribute
    struct ibv_device_attr *dev_attr;
    int rc = ibv_query_device(device->dev_ctx, dev_attr);
    if (rc != 0) {
        fprintf(stderr, "Unable to query device\n");
        exit(EXIT_FAILURE);
    }

    // todo: figure out whehter the following is required
    uint8_t dev_port = 0;
    for (; dev_port < 128; dev_port++) {
        rc = ibv_query_port(device->dev_ctx, dev_port, device->port_attr);
        if (rc == 0) break;
    }
    if (rc != 0) {
        fprintf(stderr, "Unable to query port\n");
        exit(EXIT_FAILURE);
    }

    device->dev_pd = ibv_alloc_pd(device->dev_ctx);
    if (device->dev_pd == 0) {
        fprintf(stderr, "Could not create protection domain for context\n");
        exit(EXIT_FAILURE);
    }

    // // Create shared-receive queue, **number here affect performance**.
    struct ibv_srq_init_attr srq_attr;
    srq_attr.srq_context = 0;
    // todo: maybe need to set to something else
    srq_attr.attr.max_wr = 64;
    srq_attr.attr.max_sge = 1;
    srq_attr.attr.srq_limit = 0;
    device->dev_srq = ibv_create_srq(device->dev_pd, &srq_attr);
    if (device->dev_srq == 0) {
        fprintf(stderr, "Could not create shared received queue\n");
        exit(EXIT_FAILURE);
    }

    // // Create RDMA memory.
    ibv_mem_malloc(device, 1024 * 1024 * 1024);
     if (device->heap == 0) {
        fprintf(stderr, "Unable to create heap\n");
        exit(EXIT_FAILURE);
    }
    device->heap_ptr = device->heap->addr;

    // todo: see if the following is required
    // posix_memalign((void **) &s->qp, LC_CACHE_LINE,
    //                LCI_NUM_PROCESSES * sizeof(struct ibv_qp *));

    // return FB_OK;
}

static inline int free_device(device_t *device) {
    auto ctx = ibv_open_device(device->ib_device);
    if (ctx == 0) {
        fprintf(stderr, "Unable to open ibv devices\n");
        exit(EXIT_FAILURE);
    }

    return FB_OK;
}

static inline int init_cq(device_t device, cq_t *cq) {
    cq->send_cq = ibv_create_cq(device.dev_ctx, 64 * 1024, 0, 0, 0);
    cq->recv_cq = ibv_create_cq(device.dev_ctx, 64 * 1024, 0, 0, 0);
    if (cq->send_cq == 0 || cq->send_cq == 0) {
        fprintf(stderr, "Unable to create cq\n");
        exit(EXIT_FAILURE);
    }
    return FB_OK;
}

static inline int free_cq(cq_t *cq) {
    IBV_SAFECALL(ibv_destroy_cq(cq->send_cq));
    IBV_SAFECALL(ibv_destroy_cq(cq->recv_cq));
    return FB_OK;
}

static inline int init_ctx(device_t *device, cq_t cq, ctx_t *ctx, uint64_t mode) {
    // Initialize queue pair attributes
    qp_create(device, cq);

    // todo: Call init_qp??
    return FB_OK;
}
static inline int free_ctx(ctx_t *ctx) {
    return FB_OK;
}
static inline int put_ctx_addr(ctx_t ctx, int id) {
    return FB_OK;
}

static inline int flush_ctx_addr() {
    pmi_barrier();
    return FB_OK;
}

static inline int get_ctx_addr(device_t device, int rank, int id, addr_t *addr) {
    return FB_OK;
}

static inline bool progress(cq_t cq) {
    return true;
}

static inline void isend_tag(ctx_t ctx, void *src, size_t size, addr_t target, int tag, req_t *req) {
}

static inline void irecv_tag(ctx_t ctx, void *src, size_t size, addr_t source, int tag, req_t *req) {
}
}// namespace fb

// todo: all functions return values inspect

// LC Device fields.
//   struct ibv_context* dev_ctx;
//   struct ibv_pd* dev_pd;
//   struct ibv_srq* dev_srq;
//   struct ibv_cq* send_cq;
//   struct ibv_cq* recv_cq;
//   struct ibv_mr* sbuf;
//   struct ibv_mr* heap;

//   struct ibv_port_attr port_attr;
//   struct ibv_device_attr dev_attr;

#endif//FABRICBENCH_BENCH_OFI_HPP