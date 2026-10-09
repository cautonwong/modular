#ifndef APP_DICE_PORTS_H
#define APP_DICE_PORTS_H

#include "edge/errors.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dice_entropy_if {
    void *self;
    uint32_t (*get_entropy_seed)(void *self);
} dice_entropy_if_t;

typedef struct dice_motor_if {
    void *self;
    edge_status_t (*vibrate)(void *self);
} dice_motor_if_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_DICE_PORTS_H */
