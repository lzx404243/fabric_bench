/**
* Copyright (C) Mellanox Technologies Ltd. 2001-2016.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#ifndef UCX_UTIL_H
#define UCX_UTIL_H

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
        return UCS_OK;
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
                if (!strcmp(dev_name, tl_resources[tl_index].dev_name) &&
                    !strcmp(tl_name, tl_resources[tl_index].tl_name)) {
                    status = init_iface(tl_resources[tl_index].dev_name,
                                        tl_resources[tl_index].tl_name, iface_p);
                    if (status != UCS_OK) {
                        break;
                    }
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

#endif /* UCX_UTIL_H */