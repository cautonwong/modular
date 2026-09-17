#ifndef EDGE_MODULES_H
#define EDGE_MODULES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Central module ID allocation table (D55).
 *
 * Module IDs use the same `0xNN00` segment scheme as event IDs (D31) but live
 * in their own namespace: a module ID identifies a module, an event ID
 * identifies a fact. Both are allocated centrally so a module can never pick a
 * colliding number.
 */
#define EDGE_MOD_DLT645 0x1000u
#define EDGE_MOD_DLMS 0x1100u
#define EDGE_MOD_MODBUS 0x1300u
#define EDGE_MOD_RELAY 0x2000u

/* High byte of a module/event segment, used by EDGE_ERR() in edge/errors.h. */
#define EDGE_MODULE_SEGMENT(id) ((uint32_t)(id) & 0xFF00u)

_Static_assert(EDGE_MODULE_SEGMENT(EDGE_MOD_DLT645) == EDGE_MOD_DLT645,
               "module ID must sit on a 0xNN00 segment boundary");
_Static_assert(EDGE_MODULE_SEGMENT(EDGE_MOD_DLMS) == EDGE_MOD_DLMS,
               "module ID must sit on a 0xNN00 segment boundary");
_Static_assert(EDGE_MODULE_SEGMENT(EDGE_MOD_MODBUS) == EDGE_MOD_MODBUS,
               "module ID must sit on a 0xNN00 segment boundary");
_Static_assert(EDGE_MODULE_SEGMENT(EDGE_MOD_RELAY) == EDGE_MOD_RELAY,
               "module ID must sit on a 0xNN00 segment boundary");
_Static_assert(EDGE_MOD_DLT645 != EDGE_MOD_DLMS, "duplicate module ID");
_Static_assert(EDGE_MOD_DLT645 != EDGE_MOD_MODBUS, "duplicate module ID");
_Static_assert(EDGE_MOD_DLT645 != EDGE_MOD_RELAY, "duplicate module ID");
_Static_assert(EDGE_MOD_DLMS != EDGE_MOD_MODBUS, "duplicate module ID");
_Static_assert(EDGE_MOD_DLMS != EDGE_MOD_RELAY, "duplicate module ID");
_Static_assert(EDGE_MOD_MODBUS != EDGE_MOD_RELAY, "duplicate module ID");

#ifdef __cplusplus
}
#endif

#endif
