#include <infiniband/verbs.h>
#include <string.h>

namespace fb {

static inline struct ibv_mr *ibv_mem_malloc(device_t *device, size_t size) {
    int mr_flags =
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
    void *ptr = 0;
    posix_memalign(&ptr, 8192, size + 8192);
    return ibv_reg_mr(device->dev_pd, ptr, size, mr_flags);
}

static inline void qp_create(device_t *device, cq_t cq) {
    struct ibv_qp_init_attr qp_init_attr;
    qp_init_attr.qp_context = 0;
    qp_init_attr.send_cq = cq.send_cq;
    qp_init_attr.recv_cq = cq.recv_cq;
    qp_init_attr.srq = device->dev_srq;
    qp_init_attr.cap.max_send_wr = 256;//(uint32_t)dev_attr->max_qp_wr;
    qp_init_attr.cap.max_recv_wr = 1;  //(uint32_t)dev_attr->max_qp_wr;
    // -- this affect the size of (TODO:tune later).
    qp_init_attr.cap.max_send_sge = 16;// this allows 128 inline.
    qp_init_attr.cap.max_recv_sge = 1;
    qp_init_attr.cap.max_inline_data = 0;
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.sq_sig_all = 0;
    struct ibv_qp *qp = ibv_create_qp(device->dev_pd, &qp_init_attr);
    if (!qp) {
        fprintf(stderr, "Unable to create queue pair\n");
        exit(EXIT_FAILURE);
    }
    device->dev_qp = qp;
}

static inline void qp_init(struct ibv_qp *qp, int port) {
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(struct ibv_qp_attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = port;
    attr.pkey_index = 0;
    attr.qp_access_flags =
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
    int flags =
            IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
    int rc = ibv_modify_qp(qp, &attr, flags);
    if (rc != 0) {
        fprintf(stderr, "Unable to init qp\n");
        exit(EXIT_FAILURE);
    }
}

}// namespace fb