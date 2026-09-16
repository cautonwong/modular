#ifndef EDGE_ERRORS_H
#define EDGE_ERRORS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Central error allocation table (D68).
 *
 * Layout mirrors the event/module ID scheme so a module can raise its own
 * errors without ever colliding with the framework or another module:
 *
 *   framework  : -1 .. -99   (errno-style base values)
 *   per module : EDGE_ERR(<module segment>, <code>)
 *
 * The module segment is the high byte of the module ID from `edge/modules.h`
 * (e.g. EDGE_MOD_DLT645 == 0x1000 -> segment 0x10), and the code is the low
 * byte. EDGE_ERR() therefore always lands at or below -0x0100, i.e. strictly
 * outside the framework range.
 */
typedef enum edge_status {
    EDGE_OK = 0,
    EDGE_EINVAL = -1,
    EDGE_ENOENT = -2,
    EDGE_EBUSY = -3,
    EDGE_ESTATE = -4,
    EDGE_EDEPEND = -5,
    EDGE_EOVERFLOW = -6,
    EDGE_ENOSPC = -7,
    EDGE_EIO = -8,
    EDGE_ENOTSUP = -9
} edge_status_t;

#define EDGE_ERR(mod, code) \
    (-(int32_t)(((uint32_t)(mod) & 0xFF00u) | ((uint32_t)(code) & 0xFFu)))

/* The framework range is reserved: module errors must stay out of it. */
_Static_assert(EDGE_ERR(0x1000u, 1u) < EDGE_EINVAL, "module error collides with framework range");
_Static_assert(EDGE_ERR(0xFF00u, 0xFFu) == -0xFFFF, "EDGE_ERR must compose segment and code");
_Static_assert((uint32_t)EDGE_EINVAL == 0xFFFFFFFFu, "framework errors stay errno-style");

#ifdef __cplusplus
}
#endif

#endif
