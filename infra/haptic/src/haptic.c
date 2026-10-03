#include "haptic/haptic.h"

void haptic_init(haptic_t *self) {
    if (self == NULL) {
        return;
    }
    *self = (haptic_t){0};
}

edge_status_t haptic_play(haptic_t *self, haptic_pattern_t pattern) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->pattern = pattern;
    self->pattern_timer_ms = 0u;
    self->step_index = 0u;
    self->is_active = (pattern != HAPTIC_PATTERN_NONE);
    self->motor_pin_state = self->is_active;

    return EDGE_OK;
}

void haptic_stop(haptic_t *self) {
    if (self == NULL) {
        return;
    }
    self->pattern = HAPTIC_PATTERN_NONE;
    self->is_active = false;
    self->motor_pin_state = false;
    self->pattern_timer_ms = 0u;
    self->step_index = 0u;
}

edge_status_t haptic_update(haptic_t *self, uint32_t dt_ms, bool *out_motor_on) {
    if (self == NULL || out_motor_on == NULL) {
        return EDGE_EINVAL;
    }

    if (!self->is_active || self->pattern == HAPTIC_PATTERN_NONE) {
        self->motor_pin_state = false;
        *out_motor_on = false;
        return EDGE_OK;
    }

    self->pattern_timer_ms += dt_ms;

    switch (self->pattern) {
    case HAPTIC_PATTERN_SHORT:
        if (self->pattern_timer_ms < 50u) {
            self->motor_pin_state = true;
        } else {
            haptic_stop(self);
        }
        break;

    case HAPTIC_PATTERN_LONG:
        if (self->pattern_timer_ms < 250u) {
            self->motor_pin_state = true;
        } else {
            haptic_stop(self);
        }
        break;

    case HAPTIC_PATTERN_DOUBLE:
        if (self->pattern_timer_ms < 50u) {
            self->motor_pin_state = true;
        } else if (self->pattern_timer_ms < 100u) {
            self->motor_pin_state = false;
        } else if (self->pattern_timer_ms < 150u) {
            self->motor_pin_state = true;
        } else {
            haptic_stop(self);
        }
        break;

    case HAPTIC_PATTERN_RINGING:
        /* 100ms ON, 200ms OFF repeating */
        if ((self->pattern_timer_ms % 300u) < 100u) {
            self->motor_pin_state = true;
        } else {
            self->motor_pin_state = false;
        }
        break;

    default:
        haptic_stop(self);
        break;
    }

    *out_motor_on = self->motor_pin_state;
    return EDGE_OK;
}
