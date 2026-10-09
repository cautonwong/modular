#ifndef APP_DICE_H
#define APP_DICE_H

#include "dice/ports.h"
#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DICE_MAX_COUNT 6u

typedef enum dice_sides {
    DICE_D2 = 2,
    DICE_D4 = 4,
    DICE_D6 = 6,
    DICE_D8 = 8,
    DICE_D10 = 10,
    DICE_D12 = 12,
    DICE_D20 = 20,
    DICE_D100 = 100,
} dice_sides_t;

typedef struct dice_app {
    edge_module_t module;
    const dice_entropy_if_t *entropy;
    const dice_motor_if_t *motor;

    uint8_t count;
    dice_sides_t sides;
    uint8_t results[DICE_MAX_COUNT];
    uint32_t total_sum;
    uint32_t prng_state;
    uint32_t roll_count;
} dice_app_t;

void dice_construct(dice_app_t *self, uint32_t module_id, uint32_t priority,
                    const dice_entropy_if_t *entropy, const dice_motor_if_t *motor);

edge_status_t dice_init(dice_app_t *self, const dice_entropy_if_t *entropy,
                        const dice_motor_if_t *motor);

edge_status_t dice_shutdown(dice_app_t *self);

edge_status_t dice_set_count(dice_app_t *self, uint8_t count);
uint8_t dice_get_count(const dice_app_t *self);

edge_status_t dice_set_sides(dice_app_t *self, dice_sides_t sides);
dice_sides_t dice_get_sides(const dice_app_t *self);

edge_status_t dice_roll(dice_app_t *self);
uint8_t dice_get_result(const dice_app_t *self, uint8_t index);
uint32_t dice_get_total_sum(const dice_app_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_DICE_H */
