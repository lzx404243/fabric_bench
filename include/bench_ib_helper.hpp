#pragma once

#include <infiniband/verbs.h>
#include <string.h>

#define PERFTEST_MAX_INLINE_SIZE 236

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
    uint32_t qp_num;
    uint16_t lid;
};

static inline struct ibv_mr *ibv_mem_malloc(device_t *device, size_t size) {
    const int PAGE_SIZE = sysconf(_SC_PAGESIZE);

    int mr_flags =
            IBV_ACCESS_LOCAL_WRITE;
    void *ptr = 0;
    // todo: check the following memory alignment
    posix_memalign(&ptr, PAGE_SIZE, size);
    return ibv_reg_mr(device->dev_pd, ptr, size, mr_flags);
}

static inline ibv_qp *qp_create(device_t *device, cq_t send_cq, cq_t recv_cq, srq_t srq) {
    {
		struct ibv_qp_init_attr qp_init_attr = {
			.send_cq = send_cq.cq,
			.recv_cq = recv_cq.cq,
			.srq     = srq.srq,
			.cap     = {
				.max_send_wr  = 512,
				.max_recv_wr  = RX_QUEUE_LEN,
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

    int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;

    int rc = ibv_modify_qp(qp, &attr, flags);
    if (rc != 0) {
        fprintf(stderr, "failed to modify QP state to RTR\n");
        exit(EXIT_FAILURE);
    }
}

static inline void qp_to_rts(struct ibv_qp *qp) {
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
}