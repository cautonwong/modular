#ifndef APP_GAME_PADDLE_PORTS_H
#define APP_GAME_PADDLE_PORTS_H

#include "edge/errors.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct paddle_motor_if {
    void *self;
    edge_status_t (*vibrate)(void *self, uint16_t duration_ms);
} paddle_motor_if_t;

typedef struct paddle_entropy_if {
    void *self;
    uint32_t (*get_random)(void *self);
} paddle_entropy_if_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_GAME_PADDLE_PORTS_H */
