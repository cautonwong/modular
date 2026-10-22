#ifndef GUARD_SYS_PORT_BAD_H
#define GUARD_SYS_PORT_BAD_H

#include <stdint.h>

/* A port struct in sys/ with no `void *self;`: the rule applied only to
 * app/, infra/ and board/ before #255, so this shape used to pass. */
typedef struct {
    void (*wake)(uint32_t reason);
} guard_sys_wake_port_t;

#endif
