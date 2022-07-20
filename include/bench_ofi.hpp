 //
 // Created by jiakunyan on 1/30/21.
 //

 #ifndef FABRICBENCH_BENCH_OFI_HPP
 #define FABRICBENCH_BENCH_OFI_HPP
 #include <cassert>

 #include <rdma/fabric.h>
 #include <rdma/fi_domain.h>
 #include <rdma/fi_endpoint.h>
 #include <rdma/fi_cm.h>
 #include <rdma/fi_errno.h>
 #include <rdma/fi_tagged.h>

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

 namespace fb {
 struct device_t {
     fi_info *info;
     fid_fabric *fabric;
     fid_domain *domain;
     fid_av *av;
     fid_mr *heap_mr;
     void *heap_ptr;
 };
 struct alignas(64) cq_t {
     fid_cq *cq;
 };
 struct alignas(64) ctx_t {
     fid_ep *ep;
     device_t *device;
 };
 struct alignas(64) addr_t {
     fi_addr_t addr;
 };

 // todo: libfabric should not have srq. find another way around it
 struct srq_t {
    void* srq = nullptr;
 };

 const addr_t ADDR_ANY = {FI_ADDR_UNSPEC};

 static inline int init_device(device_t *device, bool thread_safe) {
     // todo: make this configuration more explicit
     const char* DEV_NAME = "mlx5_2";
     char* dev_str = (char*)malloc(7);
     strcpy(dev_str, DEV_NAME);
     // Create hint.
     fi_info *hints;
     hints = fi_allocinfo();
     hints->ep_attr->type = FI_EP_RDM;
     hints->domain_attr->name = dev_str;
     hints->domain_attr->mr_mode = FI_MR_VIRT_ADDR | FI_MR_ALLOCATED | FI_MR_PROV_KEY | FI_MR_LOCAL;
     hints->domain_attr->control_progress = FI_PROGRESS_MANUAL;
     hints->domain_attr->data_progress = FI_PROGRESS_MANUAL;
     if (thread_safe)
         hints->domain_attr->threading = FI_THREAD_SAFE;
     hints->mode = FI_LOCAL_MR;
     hints->rx_attr->size = RX_QUEUE_LEN;

     // Create info.
     FI_SAFECALL(fi_getinfo(FI_VERSION(1, 6), nullptr, nullptr, 0, hints, &device->info));
     fi_freeinfo(hints);

     // Create libfabric obj.
     FI_SAFECALL(fi_fabric(device->info->fabric_attr, &device->fabric, nullptr));

     // Create domain.
     FI_SAFECALL(fi_domain(device->fabric, device->info, &device->domain, nullptr));

     // Get memory for heap.
     posix_memalign(&device->heap_ptr, 4096, HEAP_SIZE);
     FI_SAFECALL(fi_mr_reg(device->domain, device->heap_ptr, HEAP_SIZE,
                           FI_READ | FI_WRITE | FI_REMOTE_WRITE, 0, 0, 0,
                           &device->heap_mr, 0));

     struct fi_av_attr av_attr {
         .type = FI_AV_MAP
     };
     FI_SAFECALL(fi_av_open(device->domain, &av_attr, &device->av, nullptr));
//          printf("%s\n%s\n%s\n%s\n",
//                 device->info->domain_attr->name,
//                 device->info->fabric_attr->name,
//                 device->info->fabric_attr->prov_name,
//                 device->info->nic->device_attr->name);
//          printf("rx size: %d\n", device->info->rx_attr->size);
     pmi_barrier();
     return FB_OK;
 }

 static inline int free_device(device_t *device) {
     FI_SAFECALL(fi_close((fid_t) device->av));
     FI_SAFECALL(fi_close((fid_t) device->heap_mr));
     FI_SAFECALL(fi_close((fid_t) device->domain));
     FI_SAFECALL(fi_close((fid_t) device->fabric));
     fi_freeinfo(device->info);
     free(device->heap_ptr);
     return FB_OK;
 }

 static inline int init_cq(device_t device, cq_t *cq) {
     fi_cq_attr cq_attr{
             .size = CQ_SIZE,
             .format = FI_CQ_FORMAT_CONTEXT,
     };
     FI_SAFECALL(fi_cq_open(device.domain, &cq_attr, &cq->cq, nullptr));
     return FB_OK;
 }

 static inline int free_cq(cq_t *cq) {
     FI_SAFECALL(fi_close((fid_t) cq->cq));
     return FB_OK;
 }

 static inline int init_ctx(device_t *device, cq_t send_cq, cq_t recv_cq, srq_t /*srq*/, ctx_t *ctx, uint64_t mode) {
     // For bidirectional ep, separate cqs for send and receive
     // For send-only ep and recv-only ep, one cq is used but needs to support both send and receive to match the device endpoint capabilities
     uint64_t send_cq_flags = (mode & CTX_TX) && (mode & CTX_RX) ? FI_SEND : FI_SEND | FI_RECV;
     uint64_t recv_cq_flags = (mode & CTX_TX) && (mode & CTX_RX) ? FI_RECV : FI_SEND | FI_RECV;
     FI_SAFECALL(fi_endpoint(device->domain, device->info, &ctx->ep, nullptr));
     if (mode & CTX_TX) {
         FI_SAFECALL(fi_ep_bind(ctx->ep, (fid_t) send_cq.cq, send_cq_flags));
     }
     if (mode & CTX_RX) {
         FI_SAFECALL(fi_ep_bind(ctx->ep, (fid_t) recv_cq.cq, recv_cq_flags));
     }
     FI_SAFECALL(fi_ep_bind(ctx->ep, (fid_t) device->av, 0));
     FI_SAFECALL(fi_enable(ctx->ep));
     ctx->device = device;
     return FB_OK;
 }

 static inline void connect_ctx(ctx_t &ctx, addr_t target) {
 }

 static inline int free_ctx(ctx_t *ctx) {
     FI_SAFECALL(fi_close((fid_t) ctx->ep));
     return FB_OK;
 }

 static inline int get_num_ctx_addr(int num_sender, int num_receiver) {
     // the receiver(destination) needs to be addressed
     return num_receiver;
 }

 static inline ctx_t* get_exchanged_ctxs(ctx_t* tx_ctxs, ctx_t* rx_ctxs) {
     return rx_ctxs;
 }

 static inline int init_srq(device_t device, srq_t *srq) {
     // return without doing anything as libfabric doesn't have srq
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

     int ret = fi_av_insert(ctx.device->av, (void *) my_addr, 1, &(addr->addr), 0, nullptr);
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
     int ret = fi_av_insert(device.av, (void *) peer_addr, 1, &addr->addr, 0, nullptr);
     assert(ret == 1);
     return FB_OK;
 }

 static inline int progress(cq_t cq) {
     const int numToPoll = 16;
     fi_cq_entry entries[numToPoll];
     fi_cq_err_entry error;
     ssize_t ret = fi_cq_read(cq.cq, entries, numToPoll);
     if (ret > 0) {
         return ret;
     } else if (ret == -FI_EAGAIN) {
     } else {
         assert(ret == -FI_EAVAIL);
         fi_cq_readerr(cq.cq, &error, 0);
         printf("Err: %s\n", fi_strerror(error.err));
         exit(-1);
     }
     return 0;
 }

 // may be removed
 static inline void isend_tag(ctx_t ctx, void *src, size_t size, addr_t target, int tag, req_t *req) {
     void *desc = fi_mr_desc(ctx.device->heap_mr);
     int ret;
     do {
         ret = fi_tsend(ctx.ep, src, size, desc, target.addr, tag, req);
     } while (ret == -FI_EAGAIN);
     if (ret) FI_SAFECALL(ret);
 }

 static inline void isend(ctx_t ctx, void *src, size_t size, addr_t target) {
     void *desc = fi_mr_desc(ctx.device->heap_mr);
     int ret;
     do {
         ret = fi_send(ctx.ep, src, size, desc, target.addr, nullptr);
     } while (ret == -FI_EAGAIN);
     if (ret) FI_SAFECALL(ret);
 }

 static inline void irecv(ctx_t ctx, void *src, size_t size, addr_t source, int count) {
     if (count == 0) {
         return;
     }
     void *desc = fi_mr_desc(ctx.device->heap_mr);
     int ret;
     for (int i = 0; i < count; i++) {
         do {
             ret = fi_recv(ctx.ep, src, size, desc, source.addr, nullptr);
         } while (ret == -FI_EAGAIN);
         if (ret) FI_SAFECALL(ret);
     }
 }

 // todo: check performance and may be removed
 static inline void irecv_tag(ctx_t ctx, void *src, size_t size, addr_t source, int tag, req_t *req, int count) {
     void *desc = fi_mr_desc(ctx.device->heap_mr);
     int ret;
     constexpr uint64_t IGNORE_ALL = (uint64_t) - 1;
     for (int i = 0; i < count; i++) {
         do {
             ret = fi_trecv(ctx.ep, src, size, desc, source.addr, tag, IGNORE_ALL, req);
         } while (ret == -FI_EAGAIN);
         if (ret) FI_SAFECALL(ret);
     }
 }
 } // namespace fb

 #endif//FABRICBENCH_BENCH_OFI_HPP
