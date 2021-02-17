#ifndef FABRICBENCH_BENCH_MPI_HPP
#define FABRICBENCH_BENCH_MPI_HPP
#include <cassert>

#include "comm_exp.hpp"
#include "config.hpp"
#include "mlog.h"
#include "pmi_wrapper.h"
#include <mpi.h>


#define FI_SAFECALL(x)                                                          \
    {                                                                           \
        int err = (x);                                                          \
        if (err < 0) err = -err;                                                \
        if (err) {                                                              \
            printf("err : %s (%s:%d)\n", fi_strerror(err), __FILE__, __LINE__); \
            exit(-1);                                                           \
        }                                                                       \
    }                                                                           \
    while (0)                                                                   \
        ;

enum ctx_mode_t {
    CTX_SEND,
    CTX_RECV
};
struct device_t {
    void *heap_ptr;
};
struct cq_t {
    int *cq;
};
struct ctx_t {
    int *ep;
    device_t *device;
};
struct addr_t {
    int addr;
};
enum req_type_t {
    REQ_TYPE_NULL,
    REQ_TYPE_PEND
};
struct req_t {
    alignas(64) volatile req_type_t type;// change to atomic
    char pad[64 - sizeof(req_type_t)];
};

static inline int init_device(device_t *device, bool thread_safe) {
    int comm_rank = pmi_get_rank();
    int comm_size = pmi_get_size();
    int provided = 0;
    int err = 0;

    // Check number of process
    if (comm_size != 2) {
        if (comm_rank == 0) {
            fprintf(stderr, "This test requires exactly two processes\n");
        }
        exit(-1);
    }

    // MPI initialization
    err = MPI_Init_thread(nullptr, nullptr, MPI_THREAD_MULTIPLE, &provided);
    if (err != MPI_SUCCESS || provided != MPI_THREAD_MULTIPLE) {
        MPI_Abort(MPI_COMM_WORLD, 2);
    }
    pmi_barrier();
    return FB_OK;
}

static inline int free_device(device_t *device) {
    // MPI finialize
    // todo: verify this is called in the same thread that initialize it
    MPI_Finalize();
    return FB_OK;
}

static inline int init_cq(device_t device, cq_t *cq) {
    // Just return
    return FB_OK;
}

static inline int free_cq(cq_t *cq) {
    // Just return
    return FB_OK;
}

static inline int init_ctx(device_t *device, cq_t cq, ctx_t *ctx, uint64_t mode) {
    // Just return
    return FB_OK;
}

static inline int free_ctx(ctx_t *ctx) {
    // Just return
    return FB_OK;
}

static inline int register_ctx_self(ctx_t ctx, addr_t *addr) {
    // Just return
    return FB_OK;
}

static inline int put_ctx_addr(ctx_t ctx, int id) {
    // Just return
    return FB_OK;
}

static inline int flush_ctx_addr() {
    // Just return
    return FB_OK;
}

static inline int get_ctx_addr(device_t device, int rank, int id, addr_t *addr) {
    // Just return
    return FB_OK;
}

static inline bool progress(cq_t cq, int tag) {
    // todo: Emulate with MPI_Iprobe()

    // Maintain a MPI_request array (need locks) and use MPI_testany
    int flag = 0;
    MPI_Iprobe(MPI_ANY_SOURCE, tag, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE);
    return flag;
}

static inline void isend_tag(ctx_t ctx, void *src, size_t size, addr_t target, int tag, req_t *req) {
    // todo: Use MPI_ISEND with tag
    MPI_Send(src, size, MPI_CHAR, pmi_get_rank() == 0 ? 1 : 0, tag, MPI_COMM_WORLD);
    // req->type = REQ_TYPE_PEND;
   
}

static inline void irecv_tag(ctx_t ctx, void *src, size_t size, addr_t target, int tag, req_t *req) {
    // Use MPI_IRECV with tag
    MPI_Recv(src, size, MPI_CHAR, pmi_get_rank() == 0 ? 1 : 0, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    // req->type = REQ_TYPE_PEND;
}

#endif//FABRICBENCH_BENCH_OFI_HPP
