#ifndef EDGE_PM_H
#define EDGE_PM_H

#include "errors.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Power Policy States (ADR-010, PM-001 ~ 004)
 */
typedef enum edge_pm_policy_state {
    EDGE_PM_POLICY_STATE_RUN = 0,
    EDGE_PM_POLICY_STATE_IDLE = 1,
    EDGE_PM_POLICY_STATE_LIGHT_SLEEP = 2,
    EDGE_PM_POLICY_STATE_DEEP_SLEEP = 3
} edge_pm_policy_state_t;

/**
 * @brief Wake Sources (PM-005 ~ 012)
 */
typedef enum edge_pm_wake_source {
    EDGE_PM_WAKE_NONE = 0,
    EDGE_PM_WAKE_GPIO = 1u << 0,
    EDGE_PM_WAKE_RTC = 1u << 1,
    EDGE_PM_WAKE_TIMER = 1u << 2,
    EDGE_PM_WAKE_UART = 1u << 3,
    EDGE_PM_WAKE_BLE = 1u << 4,
    EDGE_PM_WAKE_IMU = 1u << 5,
    EDGE_PM_WAKE_TOUCH = 1u << 6
} edge_pm_wake_source_t;

/**
 * @brief Wake Lock IDs (PM-013 ~ 018)
 */
typedef enum edge_pm_wake_lock {
    EDGE_PM_LOCK_BLE_TX = 1u << 0,
    EDGE_PM_LOCK_SPI = 1u << 1,
    EDGE_PM_LOCK_FLASH_WRITE = 1u << 2,
    EDGE_PM_LOCK_DISPLAY = 1u << 3,
    EDGE_PM_LOCK_ALARM = 1u << 4
} edge_pm_wake_lock_t;

/**
 * @brief Power Management Policy Manager (PM-013 ~ 021)
 */
typedef struct edge_pm_policy {
    uint32_t active_wake_sources;
    uint32_t active_wake_locks;
    edge_pm_policy_state_t current_state;
    uint64_t next_deadline_ticks;
    uint32_t last_wake_reason;
} edge_pm_policy_t;

static inline void edge_pm_policy_init(edge_pm_policy_t *pm) {
    if (pm == NULL) {
        return;
    }
    pm->active_wake_sources = 0;
    pm->active_wake_locks = 0;
    pm->current_state = EDGE_PM_POLICY_STATE_RUN;
    pm->next_deadline_ticks = 0;
    pm->last_wake_reason = 0;
}

static inline void edge_pm_policy_lock_acquire(edge_pm_policy_t *pm, uint32_t lock_mask) {
    if (pm != NULL) {
        pm->active_wake_locks |= lock_mask;
    }
}

static inline void edge_pm_policy_lock_release(edge_pm_policy_t *pm, uint32_t lock_mask) {
    if (pm != NULL) {
        pm->active_wake_locks &= ~lock_mask;
    }
}

static inline bool edge_pm_policy_has_locks(const edge_pm_policy_t *pm) {
    return pm != NULL && pm->active_wake_locks != 0;
}

static inline void edge_pm_policy_set_wake_source(edge_pm_policy_t *pm, uint32_t source_mask,
                                                  bool enable) {
    if (pm == NULL) {
        return;
    }
    if (enable) {
        pm->active_wake_sources |= source_mask;
    } else {
        pm->active_wake_sources &= ~source_mask;
    }
}

static inline void edge_pm_policy_record_wake_reason(edge_pm_policy_t *pm, uint32_t source_mask) {
    if (pm != NULL) {
        pm->last_wake_reason = source_mask;
    }
}

static inline uint32_t edge_pm_policy_get_wake_reason(const edge_pm_policy_t *pm) {
    return pm != NULL ? pm->last_wake_reason : 0u;
}

static inline edge_pm_policy_state_t
edge_pm_policy_evaluate_state(const edge_pm_policy_t *pm, bool queue_empty, uint64_t now_ticks) {
    if (pm == NULL || !queue_empty || pm->active_wake_locks != 0) {
        return EDGE_PM_POLICY_STATE_RUN;
    }
    if (pm->next_deadline_ticks > now_ticks) {
        uint64_t remaining = pm->next_deadline_ticks - now_ticks;
        if (remaining > 5000u) {
            return EDGE_PM_POLICY_STATE_DEEP_SLEEP;
        }
        if (remaining > 100u) {
            return EDGE_PM_POLICY_STATE_LIGHT_SLEEP;
        }
    }
    return EDGE_PM_POLICY_STATE_IDLE;
}

#ifdef __cplusplus
}
#endif

#endif /* EDGE_PM_H */
