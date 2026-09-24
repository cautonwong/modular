#ifndef EDGE_MODULES_H
#define EDGE_MODULES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Central module ID table (D55, amended).
 *
 * A module ID identifies a module, an event ID identifies a fact (D31). Both use
 * the `0xNN00` segment scheme but live in their own namespaces, and both are
 * allocated centrally so a module can never pick a colliding number.
 *
 * Segments are handed out in **blocks per owning layer**, so authors working in
 * parallel do not compete for one number:
 *
 *   0x10xx - 0x2Fxx   app modules (protocol, domain, actuation)
 *   0x30xx - 0x3Fxx   infra device drivers
 *   0x40xx - 0x4Fxx   sys / runner
 *   0x50xx - 0x5Fxx   board and soc
 *   0x00xx, 0x60xx - 0xFFxx   reserved: not allocatable
 *
 * To allocate an ID, add **one line** to EDGE_MODULE_IDS() below. Nothing else:
 * the constant, the segment-alignment assertion and the block assertion are
 * generated from that line, so there is no list of pairwise assertions to keep in
 * step (the previous form needed N*(N-1)/2 of them). `check_module_ids.py`
 * rejects a duplicate, a misaligned value, a value outside its layer's block, and
 * a layer token that has no block.
 *
 * The owning layer is declared here, not inferred from a directory name: a module
 * such as DLMS or MODBUS has no directory of its own, and inferring from names
 * would be guessing.
 */
#define EDGE_MODULE_IDS(X)                                                                         \
    X(DLT645, 0x1000, app)                                                                         \
    X(DLMS, 0x1100, app)                                                                           \
    X(MODBUS, 0x1300, app)                                                                         \
    X(METER, 0x1400, app)                                                                          \
    X(PULSE_METER, 0x1500, app)                                                                    \
    X(RELAY, 0x2000, app)                                                                          \
    X(FOC_CORE, 0x2100, app)                                                                       \
    X(VESC_COMM, 0x2200, app)                                                                      \
    X(MOTOR_CONFIG, 0x2300, app)                                                                   \
    X(TIMEOUT_GUARD, 0x2400, app)                                                                  \
    X(THROTTLE, 0x2500, app)                                                                       \
    X(MOTOR_ID, 0x2600, app)                                                                       \
    X(PPM, 0x2700, app)                                                                            \
    X(ADC_INPUT, 0x2800, app)                                                                      \
    X(VESC_CAN, 0x2900, app)

/* One block per layer token used above, inclusive. */
#define EDGE_MODULE_BLOCK_app_LO 0x1000u
#define EDGE_MODULE_BLOCK_app_HI 0x2FFFu
#define EDGE_MODULE_BLOCK_infra_LO 0x3000u
#define EDGE_MODULE_BLOCK_infra_HI 0x3FFFu
#define EDGE_MODULE_BLOCK_sys_LO 0x4000u
#define EDGE_MODULE_BLOCK_sys_HI 0x4FFFu
#define EDGE_MODULE_BLOCK_board_LO 0x5000u
#define EDGE_MODULE_BLOCK_board_HI 0x5FFFu

#define EDGE_MODULE_DEFINE(name, segment, layer) EDGE_MOD_##name = (segment),

enum { EDGE_MODULE_IDS(EDGE_MODULE_DEFINE) };

#undef EDGE_MODULE_DEFINE

/* High byte of a module/event segment, used by EDGE_ERR() in edge/errors.h. */
#define EDGE_MODULE_SEGMENT(id) ((uint32_t)(id)&0xFF00u)

/* Generated, one triple per table entry: an unnamed layer has no block, so it
 * fails to compile rather than silently passing. */
#define EDGE_MODULE_ASSERT(name, segment, layer)                                                   \
    _Static_assert((segment) != 0, #name ": module ID 0 is reserved");                             \
    _Static_assert(EDGE_MODULE_SEGMENT(segment) == (segment),                                      \
                   #name ": module ID must sit on a 0xNN00 segment boundary");                     \
    _Static_assert((segment) >= EDGE_MODULE_BLOCK_##layer##_LO &&                                  \
                       (segment) <= EDGE_MODULE_BLOCK_##layer##_HI,                                \
                   #name ": module ID is outside the block allocated to its layer");

EDGE_MODULE_IDS(EDGE_MODULE_ASSERT)

#undef EDGE_MODULE_ASSERT

#ifdef __cplusplus
}
#endif

#endif
