#include <infiniband/verbs.h>

namespace fb {

#define IBV_SAFECALL(x)                                               \
  {                                                                   \
    int err = (x);                                                    \
    if (err) {                                                        \
      fprintf(stderr, "err : %d (%s:%d)\n", err, __FILE__, __LINE__); \
      exit(EXIT_FAILURE);                                             \
    }                                                                 \
  }                                                                   \
  while (0)                                                           \
    ;
struct device_t {
    ibv_device *ib_device;
};

struct cq_t {
};
struct ctx_t {
    ibv_context* dev_ctx;
    device_t *device;
};
struct addr_t {
};

// todo: the req_type_t should be available during compilation
struct req_t {
    alignas(64) volatile req_type_t type;// change to atomic
    char pad[64 - sizeof(req_type_t)];
};


// todo: check if the following is required
//onst addr_t ADDR_ANY = {FI_ADDR_UNSPEC};

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
    
    
    return 0;
}

static inline int free_device(device_t *device) {
    auto ctx = ibv_open_device(device->ib_device);
    if (s->dev_ctx == 0) {
        fprintf(stderr, "Unable to find any ibv devices\n");
        exit(EXIT_FAILURE);
    }
    ibv_free_device_list(dev_list);
    return 0;
}

static inline int init_ctx(device_t *device, cq_t cq, ctx_t *ctx, uint64_t mode) {


}

} // namespace fb



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
