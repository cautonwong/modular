#ifndef APP_CALCULATOR_H
#define APP_CALCULATOR_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CALCULATOR_FIXED_POINT_OFFSET 1000000LL /* 6 decimal places */
#define CALCULATOR_MAX_VALUE 999999999999999LL

typedef enum calculator_op {
    CALC_OP_NONE = 0,
    CALC_OP_ADD = 1,
    CALC_OP_SUB = 2,
    CALC_OP_MUL = 3,
    CALC_OP_DIV = 4,
} calculator_op_t;

typedef enum calculator_error {
    CALC_ERR_NONE = 0,
    CALC_ERR_DIV_BY_ZERO = 1,
    CALC_ERR_OVERFLOW = 2,
} calculator_error_t;

typedef struct calculator_app {
    edge_module_t module;

    int64_t current_value;
    int64_t accumulated_result;
    calculator_op_t active_op;
    calculator_error_t error;

    int64_t decimal_offset;
    bool decimal_active;
    bool new_number_started;
} calculator_app_t;

void calculator_construct(calculator_app_t *self, uint32_t module_id, uint32_t priority);
edge_status_t calculator_init(calculator_app_t *self);
edge_status_t calculator_shutdown(calculator_app_t *self);

void calculator_clear(calculator_app_t *self);
void calculator_input_digit(calculator_app_t *self, uint8_t digit);
void calculator_input_dot(calculator_app_t *self);
void calculator_input_sign_flip(calculator_app_t *self);
void calculator_input_backspace(calculator_app_t *self);
void calculator_input_op(calculator_app_t *self, calculator_op_t op);
void calculator_input_equals(calculator_app_t *self);

int64_t calculator_get_display_value(const calculator_app_t *self);
int64_t calculator_get_result(const calculator_app_t *self);
calculator_error_t calculator_get_error(const calculator_app_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_CALCULATOR_H */
