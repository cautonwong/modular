#ifndef EDGE_DESC_H
#define EDGE_DESC_H

#include "errors.h"
#include "module.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Module Descriptor (ADR-003, LIFE-004)
 *
 * Defines explicit metadata, dependency requirements, and lifecycle contracts
 * for an edge module instance.
 */
typedef struct edge_module_desc {
    uint32_t id;
    const char *name;
    size_t instance_size;

    /* Dependencies */
    const uint32_t *dependencies;
    size_t dependency_count;

    /* Lifecycle hooks */
    edge_status_t (*init)(void *instance, const void *deps);
    edge_status_t (*start)(void *instance);
    edge_status_t (*stop)(void *instance);
    edge_status_t (*suspend)(void *instance);
    edge_status_t (*resume)(void *instance);
} edge_module_desc_t;

/**
 * @brief Initialize a module descriptor instance and its lifecycle state.
 */
static inline edge_status_t edge_module_desc_init(const edge_module_desc_t *desc, void *instance,
                                                  const void *deps) {
    if (desc == NULL || instance == NULL) {
        return EDGE_EINVAL;
    }
    if (desc->init != NULL) {
        return desc->init(instance, deps);
    }
    return EDGE_OK;
}

/**
 * @brief Start a module descriptor instance.
 */
static inline edge_status_t edge_module_desc_start(const edge_module_desc_t *desc, void *instance) {
    if (desc == NULL || instance == NULL) {
        return EDGE_EINVAL;
    }
    if (desc->start != NULL) {
        return desc->start(instance);
    }
    return EDGE_OK;
}

/**
 * @brief Stop a module descriptor instance.
 */
static inline edge_status_t edge_module_desc_stop(const edge_module_desc_t *desc, void *instance) {
    if (desc == NULL || instance == NULL) {
        return EDGE_EINVAL;
    }
    if (desc->stop != NULL) {
        return desc->stop(instance);
    }
    return EDGE_OK;
}

/**
 * @brief Suspend a module descriptor instance (low-power entry).
 */
static inline edge_status_t edge_module_desc_suspend(const edge_module_desc_t *desc,
                                                     void *instance) {
    if (desc == NULL || instance == NULL) {
        return EDGE_EINVAL;
    }
    if (desc->suspend != NULL) {
        return desc->suspend(instance);
    }
    return EDGE_OK;
}

/**
 * @brief Resume a module descriptor instance (low-power exit).
 */
static inline edge_status_t edge_module_desc_resume(const edge_module_desc_t *desc,
                                                    void *instance) {
    if (desc == NULL || instance == NULL) {
        return EDGE_EINVAL;
    }
    if (desc->resume != NULL) {
        return desc->resume(instance);
    }
    return EDGE_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* EDGE_DESC_H */
