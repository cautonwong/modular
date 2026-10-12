#ifndef EDGE_EVENTS_H
#define EDGE_EVENTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Framework event constants (D31, decentralized).
 *
 * Microkernel and framework-level event IDs.
 * App business events are declared in their respective domain headers
 * and must not be registered centrally in this file.
 */
#define EDGE_EVT_NONE 0x0000u
#define EDGE_EVT_SYSTEM_START 0x0001u
#define EDGE_EVT_SYSTEM_STOP 0x0002u

#define EDGE_EVT_FRAMEWORK_BASE 0x0000u
#define EDGE_EVT_BOARD_BASE 0x0100u

#ifndef EDGE_EVT_UART0_RX
#define EDGE_EVT_UART0_RX (EDGE_EVT_BOARD_BASE + 0x01u)
#endif
#ifndef EDGE_EVT_UART0_TX_DONE
#define EDGE_EVT_UART0_TX_DONE (EDGE_EVT_BOARD_BASE + 0x02u)
#endif
#ifndef EDGE_EVT_BOARD_TIMER0
#define EDGE_EVT_BOARD_TIMER0 (EDGE_EVT_BOARD_BASE + 0x03u)
#endif

#ifdef __cplusplus
}
#endif

#endif
