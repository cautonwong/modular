#ifndef GUARD_VENDOR_PORT_H
#define GUARD_VENDOR_PORT_H

#include <stdint.h>

/* Third-party code. If the port-shape rule scanned vendor trees, this struct would
 * fail it - and the only fix would be to edit a file we do not own (#262 finding 4). */
typedef struct {
    void (*read)(uint8_t reg);
} vendor_imu_port_t;

#endif
