#ifndef EDGE_PRODUCT_H
#define EDGE_PRODUCT_H

#include "desc.h"
#include "errors.h"
#include "module.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Product Module Registration (ADR-004, APPSET-001)
 *
 * Defines an assembled module instance within a product composition root.
 */
typedef struct edge_product_module {
    const edge_module_desc_t *desc;
    void *instance;
    edge_module_t *module;
    const void *deps;
} edge_product_module_t;

/**
 * @brief Product Composition Root Descriptor (ADR-004, PRODUCT-001)
 */
typedef struct edge_product {
    const char *name;
    const edge_product_module_t *modules;
    size_t module_count;
    edge_status_t (*platform_init)(void);
    void (*platform_deinit)(void);
} edge_product_t;

/**
 * @brief Initialize all modules registered in a product composition in forward order.
 * If any module fails to initialize, previously initialized modules are rolled back
 * in reverse order (D51 rollback guarantee).
 */
static inline edge_status_t edge_product_init(const edge_product_t *product) {
    if (product == NULL) {
        return EDGE_EINVAL;
    }
    if (product->platform_init != NULL) {
        edge_status_t st = product->platform_init();
        if (st != EDGE_OK) {
            return st;
        }
    }

    for (size_t i = 0; i < product->module_count; ++i) {
        const edge_product_module_t *pm = &product->modules[i];
        if (pm->desc != NULL && pm->desc->init != NULL) {
            edge_status_t st = pm->desc->init(pm->instance, pm->deps);
            if (st != EDGE_OK) {
                /* Rollback previously initialized modules in reverse order */
                for (size_t j = i; j > 0; --j) {
                    const edge_product_module_t *prev = &product->modules[j - 1];
                    if (prev->desc != NULL && prev->desc->stop != NULL) {
                        (void)prev->desc->stop(prev->instance);
                    }
                }
                if (product->platform_deinit != NULL) {
                    product->platform_deinit();
                }
                return st;
            }
        }
    }
    return EDGE_OK;
}

/**
 * @brief Shutdown all modules registered in a product composition in reverse order.
 */
static inline void edge_product_shutdown(const edge_product_t *product) {
    if (product == NULL) {
        return;
    }
    for (size_t i = product->module_count; i > 0; --i) {
        const edge_product_module_t *pm = &product->modules[i - 1];
        if (pm->desc != NULL && pm->desc->stop != NULL) {
            (void)pm->desc->stop(pm->instance);
        }
    }
    if (product->platform_deinit != NULL) {
        product->platform_deinit();
    }
}

#ifdef __cplusplus
}
#endif

#endif /* EDGE_PRODUCT_H */
