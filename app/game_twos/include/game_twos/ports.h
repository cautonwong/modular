#ifndef APP_GAME_TWOS_PORTS_H
#define APP_GAME_TWOS_PORTS_H

#include "edge/errors.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct twos_entropy_if {
    void *self;
    uint32_t (*get_random)(void *self);
} twos_entropy_if_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_GAME_TWOS_PORTS_H */
