#pragma once

#include "hello_world_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>
#include <atomic>
#include <vector>

#include <limits.h>

#include <assert.h>
#include <inttypes.h>

namespace fb {

size_t max_am_short_size;

// todo: this may not be needed, remove
struct device_t {
    // the addr_t is the buffers storing addresses. Their respective length is stored in if_info
    uct_device_addr_t *placeholder;
};

#include "hello_world_util.h"

struct alignas(64) ctx_t {
    ucs_async_context_t *async; /* Async event context manages times and fd notifications */
    std::vector<uct_ep_addr_t *> own_ep_addrs;
    std::vector<uct_ep_h> eps;  /* handle to remote endpoint */
    iface_info_t if_info; // interface is associated with a specific worker and memory domain
    // the addr_t is the buffers storing addresses. Their respective length is stored in if_info
    uct_iface_addr_t *own_iface;
    uct_device_addr_t *own_dev;
};

struct alignas(64) addr_t {
    uct_iface_addr_t *peer_iface = NULL;
    uct_device_addr_t *peer_dev = NULL;
    std::vector<uct_ep_addr_t *> peer_ep_addrs;
};

struct req_t {
    alignas(64) volatile req_type_t type;// change to atomic
    char pad[64 - sizeof(req_type_t)];
};

struct alignas(64) buffer_t {
    char* buf;
};

static inline int init_iface(ctx_t* ctx) {
    /* Search for the desired transport */
    ucs_status_t status; /* status codes for UCS */
    // todo: remove hard-coded device name and transport layer name or move them to somewhere else
    static constexpr char* device_name = "mlx5_2:1";
    static constexpr char* tl_name = "rc_verbs";
    status = dev_tl_lookup(device_name, tl_name, &ctx->if_info);
    CHKERR_MSG(UCS_OK != status, "find supported device and transport");
    // init memory for dev and iface address
    ctx->own_dev = (uct_device_addr_t *) calloc(1, ctx->if_info.iface_attr.device_addr_len);
    CHKERR_MSG(NULL == ctx->own_dev, "allocate memory for dev addr");
    ctx->own_iface = (uct_iface_addr_t *) calloc(1, ctx->if_info.iface_attr.iface_addr_len);
    CHKERR_MSG(NULL == ctx->own_iface, "allocate memory for dev addr");

    // Get device address
    if (ctx->if_info.iface_attr.device_addr_len > 0) {
        status = uct_iface_get_device_address(ctx->if_info.iface, ctx->own_dev);
        CHKERR_MSG(UCS_OK != status, "get (OWN)device address");
    }
    // Get interface address
    if (ctx->if_info.iface_attr.iface_addr_len > 0) {
        status = uct_iface_get_address(ctx->if_info.iface, ctx->own_iface);
        CHKERR_MSG(UCS_OK != status, "get interface address");
    }
    // get the max message length that can be sent via am_short
    max_am_short_size = ctx->if_info.iface_attr.cap.am.max_short;
    return FB_OK;
}
//
//static inline int free_device(device_t *device) {
//    auto ctx = ibv_close_device(device->dev_ctx);
//    if (ctx == 0) {
//        exit(EXIT_FAILURE);
//    }
//
//    return FB_OK;
//}
//

static inline void init_ep(ctx_t *ctx) {
    uct_ep_params_t ep_params;
    ucs_status_t status; /* status codes for UCS */
    ep_params.field_mask = UCT_EP_PARAM_FIELD_IFACE;
    ep_params.iface = ctx->if_info.iface;
    uct_ep_addr_t * tmp = (uct_ep_addr_t *) calloc(1, ctx->if_info.iface_attr.ep_addr_len);
    CHKERR_MSG(NULL == tmp, "can't allocate memory for ep addrs");
    /* Create new endpoint */
    uct_ep_h tmp_ep;
    status = uct_ep_create(&ep_params, &tmp_ep);
    CHKERR_MSG(UCS_OK != status, "create endpoint");

    /* Get endpoint address */
    status = uct_ep_get_address(tmp_ep, tmp);
    ctx->eps.push_back(tmp_ep);
    ctx->own_ep_addrs.push_back(tmp);
    CHKERR_MSG(UCS_OK != status, "get endpoint address");
}

static inline int init_ctx(ctx_t *ctx, int numEP) {
    ucs_status_t status;
    // Initialize async context
    status = ucs_async_context_create(UCS_ASYNC_MODE_THREAD_SPINLOCK, &ctx->async);
    CHKERR_MSG(UCS_OK != status, "init async context");
    /* Create a worker object */
    status = uct_worker_create(ctx->async, UCS_THREAD_MODE_SERIALIZED, &ctx->if_info.worker);
    CHKERR_MSG(UCS_OK != status, "create worker");
    // init interface for this worker
    init_iface(ctx);
    // create and initialize the endpoints(progress thread can have multiple)
    for (int i = 0; i < numEP; i++) {
        init_ep(ctx);
    }
    return FB_OK;
}

//
//static inline int free_ctx(ctx_t *ctx) {
//    // todo: might need to free ctx->local_conn_info
//    return FB_OK;
//}
static inline int put_ctx_addr(ctx_t ctx, int id, const char* addr_type) {
    int comm_rank = pmi_get_rank();
    char key1[256];
    char value1[256];

    unsigned char* dev_addr = (unsigned char*)ctx.own_dev;
    // todo: hard-coded as 3 bytes. parametrize it
    // put device address
    sprintf(key1, "_IB_KEY1_%s_%d_%d", addr_type, comm_rank, id);
    sprintf(value1, "%02x-%02x-%02x", dev_addr[0], dev_addr[1], dev_addr[2]);
    pmi_put(key1, value1);

    // put ep address(may have multiple ep address)
    for (int i = 0; i < ctx.own_ep_addrs.size(); i++) {
        char key2[256];
        char value2[256];
        unsigned char* ep_addr = (unsigned char*)ctx.own_ep_addrs[i];
        sprintf(key2, "_IB_KEY2_%s_%d_%d_%d", addr_type, i, comm_rank, id);
        sprintf(value2, "%02x-%02x-%02x-%02x-%02x", ep_addr[0], ep_addr[1], ep_addr[2], ep_addr[3], ep_addr[4]);
        pmi_put(key2, value2);
    }
    //printf("putting %s thread dev address: %d\n" , addr_type, ctx.own_dev);
    return FB_OK;
}

static inline int flush_ctx_addr() {
    pmi_barrier();
    return FB_OK;
}

static inline int get_ctx_addr(ctx_t ctx, int rank, int id, addr_t *addr, const char* addr_type) {
    char key1[256];
    char value1[256];
    // Get device address -- assuming symmetric devices
    sprintf(key1, "_IB_KEY1_%s_%d_%d", addr_type, rank, id);
    pmi_get(key1, value1);
    addr->peer_dev = (uct_device_addr_t *) calloc(1, ctx.if_info.iface_attr.device_addr_len);
    unsigned char* peer_dev_addr = (unsigned char*)addr->peer_dev;
    sscanf(value1, "%02x-%02x-%02x", &peer_dev_addr[0], &peer_dev_addr[1], &peer_dev_addr[2]);

    // get ep addresses(may be multiple)
    for (int i = 0; i < ctx.own_ep_addrs.size(); i++) {
        char key2[256];
        char value2[256];
        sprintf(key2, "_IB_KEY2_%s_%d_%d_%d", addr_type, i, rank, id);
        pmi_get(key2, value2);
        uct_ep_addr_t* tmp_ep_addr = (uct_ep_addr_t *) calloc(1, ctx.if_info.iface_attr.ep_addr_len);
        unsigned char* peer_ep_addr = (unsigned char*)tmp_ep_addr;
        sscanf(value2, "%02x-%02x-%02x-%02x-%02x", &peer_ep_addr[0],&peer_ep_addr[1],&peer_ep_addr[2],&peer_ep_addr[3],&peer_ep_addr[4]);
        addr->peer_ep_addrs.push_back((uct_ep_addr_t *)peer_ep_addr);
    }
    //printf("got %s thread address: %d\n", addr_type, addr->peer_dev);
    return FB_OK;
}

/* Helper data type for am_short */
typedef struct {
    uint64_t header;
    char *payload;
    size_t len;
} am_short_args_t;

/* Helper data type for am_bcopy */
typedef struct {
    char               *data;
    size_t              len;
} am_bcopy_args_t;

/* Helper function for am_short */
void am_short_params_pack(char *buf, size_t len, am_short_args_t *args) {
    args->header = *(uint64_t *) buf;
    if (len > sizeof(args->header)) {
        args->payload = (buf + sizeof(args->header));
        args->len = len - sizeof(args->header);
    } else {
        args->payload = NULL;
        args->len = 0;
    }
}

/* Pack callback for am_bcopy */
size_t am_bcopy_data_pack_cb(void *dest, void *arg)
{
    am_bcopy_args_t *bc_args = (am_bcopy_args_t *)arg;
    mem_type_memcpy(dest, bc_args->data, bc_args->len);
    return bc_args->len;
}

static inline void isend_am_short(ctx_t ctx, void *src, size_t size, int tag) {
    ucs_status_t status;
    am_short_args_t send_args;
    am_short_params_pack((char*)src, size, &send_args);
    do {
        /* Send active message to remote endpoint */
        status = uct_ep_am_short(ctx.eps[0], tag, send_args.header, send_args.payload,
                                 send_args.len);
        uct_worker_progress(ctx.if_info.worker);
    } while (status == UCS_ERR_NO_RESOURCE);
    CHKERR_MSG(UCS_OK != status, "send active msg");
}

ucs_status_t isend_am_bcopy(ctx_t ctx, void *src, size_t size, int tag)
{
    am_bcopy_args_t args;
    ssize_t len;
    args.data = (char*)src;
    args.len  = size;
    /* Send active message to remote endpoint */
    do {
        len = uct_ep_am_bcopy(ctx.eps[0], tag, am_bcopy_data_pack_cb, &args, 0);
        uct_worker_progress(ctx.if_info.worker);
    } while (len == UCS_ERR_NO_RESOURCE);
    /* Negative len is an error code */
    return (len >= 0) ? UCS_OK : (ucs_status_t)len;
}

static inline void isend_tag(ctx_t ctx, void *src, size_t size, int tag) {
    if (size <= max_am_short_size) {
        isend_am_short(ctx, src, size, tag);
    } else {
        isend_am_bcopy(ctx, src, size, tag);
    }
    return;
}
}// namespace fb