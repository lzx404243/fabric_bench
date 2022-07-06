//
// Created by jiakunyan on 2/15/21.
//

#ifndef FABRICBENCH_FABRIC_BENCH_HPP
#define FABRICBENCH_FABRIC_BENCH_HPP

#include <iostream>

namespace fb {
/**
 * Context mode
 */
enum ctx_mode_t {
    CTX_TX, /**< Transmit context */
    CTX_RX  /**< Receive context */
};

/**
 * Request status
 */
enum req_type_t {
    REQ_TYPE_NULL, /**< This request has not been associated with a transaction or the associated transaction has been completed */
    REQ_TYPE_PEND  /**< The associated transaction has not been completed */
};

/**
 * An network device, representing the boundary of resource sharing
 */
struct device_t;
/**
 * A completion queue, report the completion of asynchronous communication operations
 *
 * A completion queue is created on a device and can be associated with one or more context
 */
struct cq_t;
/**
 * A context, the unit of thread parallelism for communication
 *
 * Two threads will be implicitly serialized when accessing the same context.
 */
struct ctx_t;
/**
 * An address, serving as the source and target of message passing
 *
 * Every context will an address
 */
struct addr_t;
/**
 * A request, representing the status of its associated asynchronous communication operation
 */
struct req_t;
/**
 * A wildcard address
 */
extern const addr_t ADDR_ANY;
/**
 * Initialize a network device object.
 * @param[out] device      a pointer to the device object to be initialized.
 * @param[in]  thread_safe whether there are multiple threads concurrently access the same network resource.
 * @return FB_OK for okay, FB_ERROR for error.
 */
static inline int init_device(device_t *device, bool thread_safe);
/**
 * Free a network device.
 * @param[in] device a pointer to the device object to be freed.
 * @return FB_OK for okay, FB_ERROR for error.
 */
static inline int free_device(device_t *device);
/**
 * Initialize a completion queue object.
 * @param[in]  device a device object that the completion queue will be associated to.
 * @param[out] cq     a pointer to the completion queue object to be initialized.
 * @return FB_OK for okay, FB_ERROR for error.
 */
static inline int init_cq(device_t device, cq_t *cq);
/**
 * Free a completion queue object.
 * @param[in] cq a pointer to the completion queue object to be freed.
 * @return FB_OK for okay, FB_ERROR for error.
 */
static inline int free_cq(cq_t *cq);
/**
 * Initialize a context object
 * @param[in]  device a pointer to the device object that the context will be associated to.
 * @param[in]  cq     a completion queue object that the context will be associated to.
 * @param[out] ctx    a pointer to the context object to be initialized.
 * @param[in]  mode   CTX_TX to initialize a transmit context that can send messages.
 *                    CTX_RX to initialize a receive context that can receive messages.
 *                    CTX_TX | CTX_RX to initialize a context that can send and receive messages.
 * @return FB_OK for okay, FB_ERROR for error.
 */
static inline int init_ctx(device_t *device, cq_t cq, ctx_t *ctx, uint64_t mode);
/**
 * Free a context object.
 * @param[in] ctx a pointer to the context object to be freed.
 * @return FB_OK for okay, FB_ERROR for error.
 */
static inline int free_ctx(ctx_t *ctx);
/**
 * Register a context to the device associated to the context and get its address.
 * @param[in]  ctx  a context object to be registered.
 * @param[out] addr the address of the context object.
 * @return FB_OK for okay, FB_ERROR for error.
 */
static inline int register_ctx_self(ctx_t ctx, addr_t *addr);
/**
 * Put the context raw address to the global key-value space (managed by PMI).
 * The key is the pair (rank, id), while the value is the context raw address.
 * @param[in]  ctx the context object of which the raw address to be put.
 * @param[out] id  the id assigned to the context raw address.
 * @return FB_OK for okay, FB_ERROR for error.
 */
static inline int put_ctx_addr(ctx_t ctx, int id);
/**
 * flush the previous key-value pairs put to the global key-value space.
 * @return FB_OK for okay, FB_ERROR for error.
 */
static inline int flush_ctx_addr();
/**
 * get the raw address specified by the key (rank, id) in the global key-value space,
 * register it, and return the address.
 * @param[in]  device  a device object to register the raw address to.
 * @param[in]  rank,id specify which raw address you want to get.
 * @param[out] addr    the address representing the registered context.
 * @return FB_OK for okay, FB_ERROR for error.
 */
static inline int get_ctx_addr(device_t device, int rank, int id, addr_t *addr);
/**
 * make progress on the completion queue.
 * @param[in] cq completion queue object to make progress on.
 * @return the number of completed request.
 */
static inline int progress(cq_t cq);
/**
 * Asynchronously send a message with a tag
 * @param[in] ctx    A context object to send the message.
 *                   If multiple threads send message with different contexts, they can do it concurrently without performance penalty.
 * @param[in] src    A pointer to the start of the message buffer.
 * @param[in] size   The size of the message.
 * @param[in] target The address of the target context to be sent to.
 * @param[in] tag    The tag associated to the message.
 * @param[in] req    A pointer to a request object associated to the send operation.
 *                   The message buffer cannot be touched until the request has been completed.
 */
static inline void isend_tag(ctx_t ctx, void *src, size_t size, addr_t target, int tag, req_t *req);
/**
 * Asynchronously post a buffer to receive a message with a tag
 * @param[in] ctx    A context object to receive the message.
 *                   If multiple threads receive message with different contexts, they can do it concurrently without performance penalty.
 * @param[in] src    A pointer to the start of the buffer to hold the message.
 * @param[in] size   The size of the buffer.
 * @param[in] source The address of the source context to be received from.
 *                   Can use ADDR_ANY to receive a message from any source contexts.
 * @param[in] tag    The tag associated to the message.
 * @param[in] req    A pointer to a request object associated to the receive operation.
 *                   The message is not ready until the request has been completed.
 */
static inline void irecv_tag(ctx_t ctx, void *src, size_t size, addr_t source, int tag, req_t *req);
} // namespace fb

#include "config.hpp"
#include "pmi_wrapper.h"
#include "mlog.h"
#ifdef FB_USE_OFI
#include "bench_ofi.hpp"
#endif
#ifdef FB_USE_IBV
#include "bench_ib.hpp"
#endif

#endif//FABRICBENCH_FABRIC_BENCH_HPP
