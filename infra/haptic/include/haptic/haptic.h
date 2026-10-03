#ifndef INFRA_HAPTIC_H
#define INFRA_HAPTIC_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum haptic_pattern {
    HAPTIC_PATTERN_NONE = 0,
    HAPTIC_PATTERN_SHORT,   /* 50ms ON */
    HAPTIC_PATTERN_DOUBLE,  /* 50ms ON, 50ms OFF, 50ms ON */
    HAPTIC_PATTERN_LONG,    /* 250ms ON */
    HAPTIC_PATTERN_RINGING, /* Repeating 100ms ON, 200ms OFF */
    HAPTIC_PATTERN_CUSTOM   /* Custom duration ON */
} haptic_pattern_t;

typedef struct haptic {
    haptic_pattern_t pattern;
    uint32_t custom_duration_ms;
    uint32_t pattern_timer_ms;
    uint8_t step_index;
    bool is_active;
    bool motor_pin_state;
} haptic_t;

void haptic_init(haptic_t *self);
edge_status_t haptic_play(haptic_t *self, haptic_pattern_t pattern);
edge_status_t haptic_run_for_duration(haptic_t *self, uint32_t duration_ms);
void haptic_stop(haptic_t *self);

/**
 * Step the haptic state machine by elapsed milliseconds.
 *
 * @param self Pointer to haptic instance.
 * @param dt_ms Time elapsed in milliseconds.
 * @param out_motor_on Output bool representing the physical GPIO motor enable pin state.
 * @return EDGE_OK on success, EDGE_EINVAL on null pointer.
 */
edge_status_t haptic_update(haptic_t *self, uint32_t dt_ms, bool *out_motor_on);

#ifdef __cplusplus
}
#endif

#endif /* INFRA_HAPTIC_H */
