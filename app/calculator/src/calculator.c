#include "calculator/calculator.h"

static edge_status_t calculator_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t calculator_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t calculator_power_off(edge_module_t *module) {
    calculator_app_t *self = (calculator_app_t *)module->private_data;
    if (self != NULL) {
        calculator_clear(self);
    }
    return EDGE_OK;
}

void calculator_construct(calculator_app_t *self, uint32_t module_id, uint32_t priority) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module.module_id = module_id;
    self->module.priority = priority;
    self->module.period = 100u;
    self->module.budget = 1u;
    self->module.next_due = 0u;
    self->module.poll = calculator_poll;
    self->module.on_event = calculator_on_event;
    self->module.power_off = calculator_power_off;
    self->module.private_data = self;

    calculator_clear(self);
}

edge_status_t calculator_init(calculator_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    calculator_construct(self, 0x3900u, 50u);
    return EDGE_OK;
}

edge_status_t calculator_shutdown(calculator_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    calculator_clear(self);
    return EDGE_OK;
}

void calculator_clear(calculator_app_t *self) {
    if (self == NULL) {
        return;
    }
    self->current_value = 0;
    self->accumulated_result = 0;
    self->active_op = CALC_OP_NONE;
    self->error = CALC_ERR_NONE;
    self->decimal_offset = CALCULATOR_FIXED_POINT_OFFSET;
    self->decimal_active = false;
    self->new_number_started = false;
}

void calculator_input_digit(calculator_app_t *self, uint8_t digit) {
    if (self == NULL || digit > 9) {
        return;
    }

    if (self->error != CALC_ERR_NONE) {
        calculator_clear(self);
    }

    if (self->new_number_started) {
        self->current_value = 0;
        self->decimal_offset = CALCULATOR_FIXED_POINT_OFFSET;
        self->decimal_active = false;
        self->new_number_started = false;
    }

    int64_t sign = (self->current_value < 0) ? -1 : 1;

    if (self->decimal_active) {
        if (self->decimal_offset > 1) {
            self->current_value += sign * self->decimal_offset * digit;
            self->decimal_offset /= 10;
        }
    } else {
        if (self->current_value <= CALCULATOR_MAX_VALUE / 10) {
            self->current_value =
                self->current_value * 10 + sign * (int64_t)digit * CALCULATOR_FIXED_POINT_OFFSET;
        }
    }
}

void calculator_input_dot(calculator_app_t *self) {
    if (self == NULL) {
        return;
    }
    if (!self->decimal_active) {
        self->decimal_active = true;
        self->decimal_offset = CALCULATOR_FIXED_POINT_OFFSET / 10;
    }
}

void calculator_input_sign_flip(calculator_app_t *self) {
    if (self != NULL) {
        self->current_value = -self->current_value;
    }
}

void calculator_input_backspace(calculator_app_t *self) {
    if (self == NULL) {
        return;
    }

    if (self->decimal_active && self->decimal_offset < CALCULATOR_FIXED_POINT_OFFSET / 10) {
        self->decimal_offset *= 10;
        int64_t rem = self->current_value % (self->decimal_offset * 10);
        self->current_value -= rem;
    } else {
        self->decimal_active = false;
        self->decimal_offset = CALCULATOR_FIXED_POINT_OFFSET;
        int64_t whole = self->current_value / CALCULATOR_FIXED_POINT_OFFSET;
        whole /= 10;
        self->current_value = whole * CALCULATOR_FIXED_POINT_OFFSET;
    }
}

static void evaluate_op(calculator_app_t *self) {
    if (self->active_op == CALC_OP_NONE) {
        self->accumulated_result = self->current_value;
        return;
    }

    switch (self->active_op) {
    case CALC_OP_ADD:
        self->accumulated_result += self->current_value;
        break;
    case CALC_OP_SUB:
        self->accumulated_result -= self->current_value;
        break;
    case CALC_OP_MUL:
        self->accumulated_result = (self->accumulated_result * (self->current_value / 1000)) /
                                   (CALCULATOR_FIXED_POINT_OFFSET / 1000);
        break;
    case CALC_OP_DIV:
        if (self->current_value == 0) {
            self->error = CALC_ERR_DIV_BY_ZERO;
            self->accumulated_result = 0;
        } else {
            self->accumulated_result =
                (self->accumulated_result * (CALCULATOR_FIXED_POINT_OFFSET / 1000)) /
                (self->current_value / 1000);
        }
        break;
    default:
        break;
    }
}

void calculator_input_op(calculator_app_t *self, calculator_op_t op) {
    if (self == NULL) {
        return;
    }

    evaluate_op(self);
    self->active_op = op;
    self->new_number_started = true;
    self->current_value = self->accumulated_result;
}

void calculator_input_equals(calculator_app_t *self) {
    if (self == NULL) {
        return;
    }

    evaluate_op(self);
    self->active_op = CALC_OP_NONE;
    self->current_value = self->accumulated_result;
    self->new_number_started = true;
}

int64_t calculator_get_display_value(const calculator_app_t *self) {
    return self != NULL ? self->current_value : 0;
}

int64_t calculator_get_result(const calculator_app_t *self) {
    return self != NULL ? self->accumulated_result : 0;
}

calculator_error_t calculator_get_error(const calculator_app_t *self) {
    return self != NULL ? self->error : CALC_ERR_NONE;
}
