#pragma once

#include <infiniband/verbs.h>
#include <string.h>

#define PERFTEST_MAX_INLINE_SIZE 236

int rx_depth = 8;

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

struct conn_info {
    // the following two fields are relevant only for rdma read/write, not send
    //uint64_t addr;
    //uint32_t rkey;
    uint32_t qp_num;
    uint16_t lid;
    union ibv_gid gid;
};

static inline struct ibv_mr *ibv_mem_malloc(device_t *device, size_t size) {
    int mr_flags =
            IBV_ACCESS_LOCAL_WRITE;
    void *ptr = 0;
    // todo: check the following memory alignment
    posix_memalign(&ptr, 8192, size + 8192);
    return ibv_reg_mr(device->dev_pd, ptr, size, mr_flags);
}

static inline ibv_qp *qp_create(device_t *device, cq_t cq) {

    // todo: investigate why the commented out code cause intermittant segfault...(Does it resolve by adding a scope)
    // struct ibv_qp_init_attr qp_init_attr;
    // printf("setting up qp init attr\n");
    // //qp_init_attr.qp_context = 0;
    // qp_init_attr.send_cq = cq.cq;
    // qp_init_attr.recv_cq = cq.cq;

    // printf("done setting up srq\n");
    // qp_init_attr.cap.max_send_wr = 256;//(uint32_t)dev_attr->max_qp_wr;
    // qp_init_attr.cap.max_recv_wr = 1;  //(uint32_t)dev_attr->max_qp_wr;
    // // -- this affect the size of (TODO:tune later).
    // qp_init_attr.cap.max_send_sge = 16;// this allows 128 inline.
    // qp_init_attr.cap.max_recv_sge = 1;
    // qp_init_attr.cap.max_inline_data = 0;
    // qp_init_attr.qp_type = IBV_QPT_RC;
    // qp_init_attr.sq_sig_all = 0;

    {
		struct ibv_qp_init_attr qp_init_attr = {
			.send_cq = cq.cq,
			.recv_cq = cq.cq,
			.cap     = {
				.max_send_wr  = 1,
				.max_recv_wr  = rx_depth,
				.max_send_sge = 1,
				.max_recv_sge = 1,
                .max_inline_data = PERFTEST_MAX_INLINE_SIZE
			},
			.qp_type = IBV_QPT_RC
		};
	
    struct ibv_qp *qp = ibv_create_qp(device->dev_pd, &qp_init_attr);
    if (!qp) {
        fprintf(stderr, "Unable to create queue pair\n");
        exit(EXIT_FAILURE);
    }
    return qp;
    }
}

static inline void qp_init(struct ibv_qp *qp, int port) {
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(struct ibv_qp_attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = port;
    attr.pkey_index = 0;
    attr.qp_access_flags = 0;
    int flags =
            IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
    int rc = ibv_modify_qp(qp, &attr, flags);
    if (rc != 0) {
        fprintf(stderr, "Unable to init qp\n");
        exit(EXIT_FAILURE);
    }
}

static inline void qp_to_rtr(struct ibv_qp *qp, int dev_port,
                             struct ibv_port_attr *port_attr,
                             struct conn_info *rctx_) {
    //printf("entering qp_to_rtr\n");
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(struct ibv_qp_attr));

    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = port_attr->active_mtu;
    attr.dest_qp_num = rctx_->qp_num;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 12;

    attr.ah_attr.is_global = 0;
    attr.ah_attr.dlid = rctx_->lid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = dev_port;

    // memcpy(&attr.ah_attr.grh.dgid, &rctx_->gid, 16);
    // attr.ah_attr.grh.sgid_index = 0;// gid

    int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;

    int rc = ibv_modify_qp(qp, &attr, flags);
    if (rc != 0) {
        fprintf(stderr, "failed to modify QP state to RTR\n");
        exit(EXIT_FAILURE);
    }
    //printf("done - qp_to_rtr\n");

}

static inline void qp_to_rts(struct ibv_qp *qp) {
    //printf("entering qp_to_rts\n");

    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(struct ibv_qp_attr));

    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 0x12;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.sq_psn = 0;
    attr.max_rd_atomic = 1;

    int flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    int rc = ibv_modify_qp(qp, &attr, flags);
    if (rc != 0) {
        fprintf(stderr, "failed to modify QP state to RTS\n");
        exit(EXIT_FAILURE);
    }
    //printf("Done - qp_to_rts updated\n");
}