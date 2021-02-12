#ifndef FABRICBENCH_BENCH_MPI_HPP
#define FABRICBENCH_BENCH_MPI_HPP
#include <cassert>

#include "config.hpp"
#include "pmi_wrapper.h"
#include "comm_exp.hpp"
#include "mlog.h"


#define FI_SAFECALL(x)                                                    \
  {                                                                       \
    int err = (x);                                                        \
    if (err < 0) err = -err;                                              \
    if (err) {                                                            \
      printf("err : %s (%s:%d)\n", fi_strerror(err), __FILE__, __LINE__); \
      exit(-1);                                                           \
    }                                                                     \
  }                                                                       \
  while (0)                                                               \
    ;                                                                     \

enum ctx_mode_t {
    CTX_SEND,
    CTX_RECV
};
struct device_t {
    fi_info *info;
    fid_fabric *fabric;
    fid_domain *domain;
    fid_av *av;
    fid_mr *heap_mr;
    void *heap_ptr;
};
struct cq_t {
    fid_cq *cq;
};
struct ctx_t {
    fid_ep *ep;
    device_t *device;
};
struct addr_t {
    fi_addr_t addr;
};
enum req_type_t {
    REQ_TYPE_NULL,
    REQ_TYPE_PEND
};
struct req_t {
    alignas(64) volatile req_type_t type; // change to atomic
    char pad[64-sizeof(req_type_t)];
};

const addr_t ADDR_ANY = {FI_ADDR_UNSPEC};

static inline int init_device(device_t *device, bool thread_safe) {
    int comm_rank = pmi_get_rank();
    int comm_size = pmi_get_size();
    // todo: MPI_init
    pmi_barrier();
    return FB_OK;
}

static inline int free_device(device_t *device) {
    // MPI finialize?
    return FB_OK;
}

static inline int init_cq(device_t device, cq_t* cq) {
    // Just return
    return FB_OK;
}

static inline int free_cq(cq_t* cq) {
    // Just return
    return FB_OK;
}

static inline int init_ctx(device_t *device, cq_t cq, ctx_t* ctx, uint64_t mode) {
    // Just return
    return FB_OK;
}

static inline int free_ctx(ctx_t* ctx) {
    // Just return
    return FB_OK;
}

static inline int register_ctx_self(ctx_t ctx, addr_t *addr) {
    // Now exchange end-point address and heap address.
    const int EP_ADDR_LEN = 6;
    uint64_t my_addr[EP_ADDR_LEN];
    size_t addrlen = 0;
    fi_getname((fid_t) ctx.ep, nullptr, &addrlen);
    assert(addrlen <= 8 * EP_ADDR_LEN);
    FI_SAFECALL(fi_getname((fid_t) ctx.ep, my_addr, &addrlen));

    int ret = fi_av_insert(ctx.device->av, (void *)my_addr, 1, &(addr->addr), 0, nullptr);
    assert(ret == 1);
    return FB_OK;
}

static inline int put_ctx_addr(ctx_t ctx, int id) {
    int comm_rank = pmi_get_rank();
    char key[256];
    char value[256];

    // Now exchange end-point address and heap address.
    const int EP_ADDR_LEN = 6;
    uint64_t my_addr[EP_ADDR_LEN];
    size_t addrlen = 0;
    fi_getname((fid_t) ctx.ep, nullptr, &addrlen);
    assert(addrlen <= 8 * EP_ADDR_LEN);
    FI_SAFECALL(fi_getname((fid_t) ctx.ep, my_addr, &addrlen));

    const char *PARSE_STRING = "%016lx-%016lx-%016lx-%016lx-%016lx-%016lx";
    sprintf(key, "_FB_KEY_%d_%d", comm_rank, id);
    sprintf(value, PARSE_STRING,
            my_addr[0], my_addr[1], my_addr[2], my_addr[3], my_addr[4], my_addr[5]);

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
    const int EP_ADDR_LEN = 6;
    const char *PARSE_STRING = "%016lx-%016lx-%016lx-%016lx-%016lx-%016lx";

    uint64_t peer_addr[EP_ADDR_LEN];
    sprintf(key, "_FB_KEY_%d_%d", rank, id);
    pmi_get(key, value);
    sscanf(value, PARSE_STRING,
           &peer_addr[0], &peer_addr[1], &peer_addr[2], &peer_addr[3], &peer_addr[4], &peer_addr[5]);
    int ret = fi_av_insert(device.av, (void *)peer_addr, 1, &addr->addr, 0, nullptr);
    assert(ret == 1);
    return FB_OK;
}

static inline bool progress(cq_t cq)
{
    fi_cq_entry entry;
    fi_cq_err_entry error;
    ssize_t ret = fi_cq_read(cq.cq, &entry, 1);
    if (ret > 0) {
        req_t* r = (req_t*) entry.op_context;
        if ( r != NULL ) {
            r->type = REQ_TYPE_NULL;
            return true;
        }
    } else if (ret == -FI_EAGAIN) {
    } else {
        assert(ret == -FI_EAVAIL);
        fi_cq_readerr(cq.cq, &error, 0);
        printf("Err: %s\n", fi_strerror(error.err));
        exit(-1);
    }
    return false;
}

static inline void isend_tag(ctx_t ctx, void* src, size_t size, addr_t target, int tag, req_t* req)
{
    req->type = REQ_TYPE_PEND;
    void* desc = fi_mr_desc(ctx.device->heap_mr);
    int ret;
    do {
        ret = fi_tsend(ctx.ep, src, size, desc, target.addr, tag, req);
    } while (ret == -FI_EAGAIN);
    if (ret) FI_SAFECALL(ret);
}

static inline void irecv_tag(ctx_t ctx, void* src, size_t size, addr_t target, int tag, req_t* req)
{
    req->type = REQ_TYPE_PEND;
    void* desc = fi_mr_desc(ctx.device->heap_mr);
    int ret;
    do {
        ret = fi_trecv(ctx.ep, src, size, desc, target.addr, tag, 0x0, req);
    } while (ret == -FI_EAGAIN);
    if (ret) FI_SAFECALL(ret);
}

#endif//FABRICBENCH_BENCH_OFI_HPP
