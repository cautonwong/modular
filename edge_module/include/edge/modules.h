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
 *   0x10xx - 0x3Fxx   app modules (protocol, domain, actuation)
 *   0x40xx - 0x4Fxx   infra device drivers
 *   0x50xx - 0x5Fxx   sys / runner
 *   0x60xx - 0x6Fxx   board and soc
 *   0x00xx, 0x70xx - 0xFFxx   reserved: not allocatable
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
    X(WATCH_TIMER, 0x1200, app)                                                                    \
    X(MODBUS, 0x1300, app)                                                                         \
    X(METER, 0x1400, app)                                                                          \
    X(PULSE_METER, 0x1500, app)                                                                    \
    X(TOUCH_GESTURE, 0x1600, app)                                                                  \
    X(WATCH_POWER, 0x1700, app)                                                                    \
    X(STEP_COUNTER, 0x1800, app)                                                                   \
    X(HEART_RATE, 0x1900, app)                                                                     \
    X(WATCH_TIME, 0x1A00, app)                                                                     \
    X(WATCH_UI, 0x1B00, app)                                                                       \
    X(BUTTON_HANDLER, 0x1C00, app)                                                                 \
    X(BLE_SERVICES, 0x1D00, app)                                                                   \
    X(ALARM, 0x1E00, app)                                                                          \
    X(STOPWATCH, 0x1F00, app)                                                                      \
    X(RELAY, 0x2000, app)                                                                          \
    X(WATCH_SETTINGS, 0x2F00, app)                                                                 \
    X(BLE_WEATHER, 0x3000, app)                                                                    \
    X(BLE_MUSIC, 0x3100, app)                                                                      \
    X(BLE_NAV, 0x3200, app)                                                                        \
    X(BLE_NOTIFICATIONS, 0x3300, app)                                                              \
    X(BLE_MOTION, 0x3400, app)                                                                     \
    X(BLE_FS, 0x3500, app)                                                                         \
    X(BLE_DFU, 0x3600, app)                                                                        \
    X(FIRMWARE_VALIDATOR, 0x3700, app)                                                             \
    X(METRONOME, 0x3800, app)                                                                      \
    X(CALCULATOR, 0x3900, app)                                                                     \
    X(DICE, 0x3A00, app)                                                                           \
    X(BLE_PASSKEY, 0x3B00, app)                                                                    \
    X(FLASHLIGHT, 0x3C00, app)                                                                     \
    X(GAME_PADDLE, 0x3D00, app)                                                                    \
    X(GAME_TWOS, 0x3E00, app)                                                                      \
    X(PAINT, 0x3F00, app)

/* One block per layer token used above, inclusive. */
#define EDGE_MODULE_BLOCK_app_LO 0x1000u
#define EDGE_MODULE_BLOCK_app_HI 0x3FFFu
#define EDGE_MODULE_BLOCK_infra_LO 0x4000u
#define EDGE_MODULE_BLOCK_infra_HI 0x4FFFu
#define EDGE_MODULE_BLOCK_sys_LO 0x5000u
#define EDGE_MODULE_BLOCK_sys_HI 0x5FFFu
#define EDGE_MODULE_BLOCK_board_LO 0x6000u
#define EDGE_MODULE_BLOCK_board_HI 0x6FFFu

#define EDGE_MODULE_DEFINE(name, segment, layer) EDGE_MOD_##name = (segment),

enum { EDGE_MODULE_IDS(EDGE_MODULE_DEFINE) };

#undef EDGE_MODULE_DEFINE

/* High byte of a module/event segment, used by EDGE_ERR() in edge/errors.h. */
#define EDGE_MODULE_SEGMENT(id) ((uint32_t)(id) & 0xFF00u)

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
