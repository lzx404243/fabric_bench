/**
* Copyright (C) Mellanox Technologies Ltd. 2001-2016.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCX_HELLO_WORLD_H
#define UCX_HELLO_WORLD_H

#include <ucs/memory/memory_type.h>

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <time.h>
#include <uct/api/uct.h>
#include <unistd.h>
#include <assert.h>


#ifdef HAVE_CUDA
#include <cuda.h>
#include <cuda_runtime.h>
#endif


#define CHKERR_ACTION(_cond, _msg, _action)          \
    do {                                             \
        if (_cond) {                                 \
            fprintf(stderr, "Failed to %s\n", _msg); \
            _action;                                 \
        }                                            \
    } while (0)


#define CHKERR_MSG(_cond, _msg)                      \
    do {                                             \
        if (_cond) {                                 \
            fprintf(stderr, "Failed to %s\n", _msg); \
        }                                            \
    } while (0)


#define CHKERR_JUMP(_cond, _msg, _label)                       \
do {                                                                       \
if (_cond) {                                                           \
fprintf(stderr, "Failed to %s, return value %d\n", _msg); \
goto _label;                                                       \
}                                                                      \
} while (0)

#define CHKERR_JUMP_RETVAL(_cond, _msg, _label, _retval)                       \
    do {                                                                       \
        if (_cond) {                                                           \
            fprintf(stderr, "Failed to %s, return value %d\n", _msg, _retval); \
            goto _label;                                                       \
        }                                                                      \
    } while (0)


static ucs_memory_type_t test_mem_type = UCS_MEMORY_TYPE_HOST;

typedef struct {
    int is_uct_desc;
} recv_desc_t;

typedef struct {
    uct_iface_attr_t iface_attr; /* Interface attributes: capabilities and limitations */
    uct_iface_h iface;           /* Communication interface context */
    uct_md_attr_t md_attr;       /* Memory domain attributes: capabilities and limitations */
    uct_md_h md;                 /* Memory domain */
    uct_worker_h worker;         /* Workers represent allocated resources in a communication thread */
} iface_info_t;

#define CUDA_FUNC(_func)                                  \
    do {                                                  \
        cudaError_t _result = (_func);                    \
        if (cudaSuccess != _result) {                     \
            fprintf(stderr, "%s failed: %s\n",            \
                    #_func, cudaGetErrorString(_result)); \
        }                                                 \
    } while (0)

void print_common_help(void);

void *mem_type_malloc(size_t length) {
    void *ptr;

    switch (test_mem_type) {
        case UCS_MEMORY_TYPE_HOST:
            ptr = malloc(length);
            break;
#ifdef HAVE_CUDA
        case UCS_MEMORY_TYPE_CUDA:
            CUDA_FUNC(cudaMalloc(&ptr, length));
            break;
        case UCS_MEMORY_TYPE_CUDA_MANAGED:
            CUDA_FUNC(cudaMallocManaged(&ptr, length, cudaMemAttachGlobal));
            break;
#endif
        default:
            fprintf(stderr, "Unsupported memory type: %d\n", test_mem_type);
            ptr = NULL;
            break;
    }

    return ptr;
}

void mem_type_free(void *address) {
    switch (test_mem_type) {
        case UCS_MEMORY_TYPE_HOST:
            free(address);
            break;
#ifdef HAVE_CUDA
        case UCS_MEMORY_TYPE_CUDA:
        case UCS_MEMORY_TYPE_CUDA_MANAGED:
            CUDA_FUNC(cudaFree(address));
            break;
#endif
        default:
            fprintf(stderr, "Unsupported memory type: %d\n", test_mem_type);
            break;
    }
}

void *mem_type_memcpy(void *dst, const void *src, size_t count) {
    switch (test_mem_type) {
        case UCS_MEMORY_TYPE_HOST:
            memcpy(dst, src, count);
            break;
#ifdef HAVE_CUDA
        case UCS_MEMORY_TYPE_CUDA:
        case UCS_MEMORY_TYPE_CUDA_MANAGED:
            CUDA_FUNC(cudaMemcpy(dst, src, count, cudaMemcpyDefault));
            break;
#endif
        default:
            fprintf(stderr, "Unsupported memory type: %d\n", test_mem_type);
            break;
    }

    return dst;
}

void *mem_type_memset(void *dst, int value, size_t count) {
    switch (test_mem_type) {
        case UCS_MEMORY_TYPE_HOST:
            memset(dst, value, count);
            break;
#ifdef HAVE_CUDA
        case UCS_MEMORY_TYPE_CUDA:
        case UCS_MEMORY_TYPE_CUDA_MANAGED:
            CUDA_FUNC(cudaMemset(dst, value, count));
            break;
#endif
        default:
            fprintf(stderr, "Unsupported memory type: %d", test_mem_type);
            break;
    }

    return dst;
}

int check_mem_type_support(ucs_memory_type_t mem_type) {
    switch (test_mem_type) {
        case UCS_MEMORY_TYPE_HOST:
            return 1;
        case UCS_MEMORY_TYPE_CUDA:
        case UCS_MEMORY_TYPE_CUDA_MANAGED:
#ifdef HAVE_CUDA
            return 1;
#else
            return 0;
#endif
        default:
            fprintf(stderr, "Unsupported memory type: %d", test_mem_type);
            break;
    }

    return 0;
}

ucs_memory_type_t parse_mem_type(const char *opt_arg) {
    if (!strcmp(opt_arg, "host")) {
        return UCS_MEMORY_TYPE_HOST;
    } else if (!strcmp(opt_arg, "cuda") &&
               check_mem_type_support(UCS_MEMORY_TYPE_CUDA)) {
        return UCS_MEMORY_TYPE_CUDA;
    } else if (!strcmp(opt_arg, "cuda-managed") &&
               check_mem_type_support(UCS_MEMORY_TYPE_CUDA_MANAGED)) {
        return UCS_MEMORY_TYPE_CUDA_MANAGED;
    } else {
        fprintf(stderr, "Unsupported memory type: \"%s\".\n", opt_arg);
    }

    return UCS_MEMORY_TYPE_LAST;
}

void print_common_help() {
    fprintf(stderr, "  -p <port>     Set alternative server port (default:13337)\n");
    fprintf(stderr, "  -6            Use IPv6 address in data exchange\n");
    fprintf(stderr, "  -s <size>     Set test string length (default:16)\n");
    fprintf(stderr, "  -m <mem type> Memory type of messages\n");
    fprintf(stderr, "                host - system memory (default)\n");
    if (check_mem_type_support(UCS_MEMORY_TYPE_CUDA)) {
        fprintf(stderr, "                cuda - NVIDIA GPU memory\n");
    }
    if (check_mem_type_support(UCS_MEMORY_TYPE_CUDA_MANAGED)) {
        fprintf(stderr, "                cuda-managed - NVIDIA GPU managed/unified memory\n");
    }
}

static inline int
barrier(int oob_sock, void (*progress_cb)(void *arg), void *arg) {
    struct pollfd pfd;
    int dummy = 0;
    ssize_t res;

    res = send(oob_sock, &dummy, sizeof(dummy), 0);
    if (res < 0) {
        return res;
    }

    pfd.fd = oob_sock;
    pfd.events = POLLIN;
    pfd.revents = 0;
    do {
        res = poll(&pfd, 1, 1);
        progress_cb(arg);
    } while (res != 1);

    res = recv(oob_sock, &dummy, sizeof(dummy), MSG_WAITALL);

    /* number of received bytes should be the same as sent */
    return !(res == sizeof(dummy));
}

static inline int generate_test_string(char *str, int size) {
    char *tmp_str;
    int i;

    tmp_str = (char *) calloc(1, size);
    CHKERR_ACTION(tmp_str == NULL, "allocate memory\n", return -1);

    for (i = 0; i < (size - 1); ++i) {
        tmp_str[i] = 'A' + (i % 26);
    }

    mem_type_memcpy(str, tmp_str, size);

    free(tmp_str);
    return 0;
}


/* Init the transport by its name */
static ucs_status_t init_iface(char *dev_name, char *tl_name, iface_info_t *iface_p) {
    ucs_status_t status;
    uct_iface_config_t *config; /* Defines interface configuration options */
    uct_iface_params_t params;

    params.field_mask = UCT_IFACE_PARAM_FIELD_OPEN_MODE |
                        UCT_IFACE_PARAM_FIELD_DEVICE |
                        UCT_IFACE_PARAM_FIELD_STATS_ROOT |
                        UCT_IFACE_PARAM_FIELD_RX_HEADROOM |
                        UCT_IFACE_PARAM_FIELD_CPU_MASK;
    params.open_mode = UCT_IFACE_OPEN_MODE_DEVICE;
    params.mode.device.tl_name = tl_name;
    params.mode.device.dev_name = dev_name;
    params.stats_root = NULL;
    params.rx_headroom = sizeof(recv_desc_t);

    UCS_CPU_ZERO(&params.cpu_mask);
    /* Read transport-specific interface configuration */
    status = uct_md_iface_config_read(iface_p->md, tl_name, NULL, NULL, &config);
    CHKERR_JUMP(UCS_OK != status, "setup iface_config", error_ret);

    /* Open communication interface */
    assert(iface_p->iface == NULL);
    status = uct_iface_open(iface_p->md, iface_p->worker, &params, config,
                            &iface_p->iface);
    uct_config_release(config);
    CHKERR_JUMP(UCS_OK != status, "open temporary interface", error_ret);

    /* Enable progress on the interface */
    uct_iface_progress_enable(iface_p->iface,
                              UCT_PROGRESS_SEND | UCT_PROGRESS_RECV);

    /* Get interface attributes */
    status = uct_iface_query(iface_p->iface, &iface_p->iface_attr);
    CHKERR_JUMP(UCS_OK != status, "query iface", error_iface);

    /* Check if current device and transport support required active messages */
    if (iface_p->iface_attr.cap.flags & UCT_IFACE_FLAG_AM_SHORT) {
        if (test_mem_type != UCS_MEMORY_TYPE_CUDA) {
            return UCS_OK;
        } else {
            fprintf(stderr, "AM short protocol doesn't support CUDA memory");
        }
    }

error_iface:
    uct_iface_close(iface_p->iface);
    iface_p->iface = NULL;
error_ret:
    return UCS_ERR_UNSUPPORTED;
}


/* Device and transport to be used are determined by minimum latency */
static ucs_status_t dev_tl_lookup(const char *dev_name, const char *tl_name, iface_info_t *iface_p) {
    uct_tl_resource_desc_t *tl_resources = NULL; /* Communication resource descriptor */
    unsigned num_tl_resources = 0;               /* Number of transport resources resource objects created */
    uct_component_h *components;
    unsigned num_components;
    unsigned cmpt_index;
    uct_component_attr_t component_attr;
    unsigned md_index;
    unsigned tl_index;
    uct_md_config_t *md_config;
    ucs_status_t status;

    status = uct_query_components(&components, &num_components);
    CHKERR_JUMP(UCS_OK != status, "query for components", error_ret);

    for (cmpt_index = 0; cmpt_index < num_components; ++cmpt_index) {

        component_attr.field_mask = UCT_COMPONENT_ATTR_FIELD_MD_RESOURCE_COUNT;
        status = uct_component_query(components[cmpt_index], &component_attr);
        CHKERR_JUMP(UCS_OK != status, "query component attributes",
                    release_component_list);

        component_attr.field_mask = UCT_COMPONENT_ATTR_FIELD_MD_RESOURCES;
        component_attr.md_resources = (uct_md_resource_desc_t *) alloca(sizeof(*component_attr.md_resources) *
                                                                        component_attr.md_resource_count);
        status = uct_component_query(components[cmpt_index], &component_attr);
        CHKERR_JUMP(UCS_OK != status, "query for memory domain resources",
                    release_component_list);

        iface_p->iface = NULL;

        /* Iterate through memory domain resources */
        for (md_index = 0; md_index < component_attr.md_resource_count; ++md_index) {
            status = uct_md_config_read(components[cmpt_index], NULL, NULL,
                                        &md_config);
            CHKERR_JUMP(UCS_OK != status, "read MD config",
                        release_component_list);

            status = uct_md_open(components[cmpt_index],
                                 component_attr.md_resources[md_index].md_name,
                                 md_config, &iface_p->md);
            uct_config_release(md_config);
            CHKERR_JUMP(UCS_OK != status, "open memory domains",
                        release_component_list);

            status = uct_md_query(iface_p->md, &iface_p->md_attr);
            CHKERR_JUMP(UCS_OK != status, "query iface",
                        close_md);

            status = uct_md_query_tl_resources(iface_p->md, &tl_resources,
                                               &num_tl_resources);
            CHKERR_JUMP(UCS_OK != status, "query transport resources", close_md);

            /* Go through each available transport and find the proper name */
            for (tl_index = 0; tl_index < num_tl_resources; ++tl_index) {
                //printf("dev name: %s, transport name: %s\n", tl_resources[tl_index].dev_name, tl_resources[tl_index].tl_name);
                if (!strcmp(dev_name, tl_resources[tl_index].dev_name) &&
                    !strcmp(tl_name, tl_resources[tl_index].tl_name)) {
                    //printf("found matching dev. initing iface\n");
                    status = init_iface(tl_resources[tl_index].dev_name,
                                        tl_resources[tl_index].tl_name, iface_p);
                    //printf("done initing iface\n");

                    if (status != UCS_OK) {
                        break;
                    }

                    //fprintf(stdout, "Using " UCT_TL_RESOURCE_DESC_FMT "\n",UCT_TL_RESOURCE_DESC_ARG(&tl_resources[tl_index]));
                    goto release_tl_resources;
                }
            }

        release_tl_resources:
            uct_release_tl_resource_list(tl_resources);
            if ((status == UCS_OK) &&
                (tl_index < num_tl_resources)) {
                goto release_component_list;
            }

            tl_resources = NULL;
            num_tl_resources = 0;
            uct_md_close(iface_p->md);
        }
    }

    fprintf(stderr, "No supported (dev/tl) found (%s/%s)\n",
            dev_name, tl_name);
    status = UCS_ERR_UNSUPPORTED;

release_component_list:
    uct_release_component_list(components);
error_ret:
    return status;
close_md:
    uct_md_close(iface_p->md);
    goto release_component_list;
}

#endif /* UCX_HELLO_WORLD_H */