#ifndef EDGE_EVENTS_H
#define EDGE_EVENTS_H

#define EDGE_EVT_FRAMEWORK_BASE 0x0000u
#define EDGE_EVT_BOARD_BASE 0x0100u
#define EDGE_EVT_DLT645_BASE 0x1000u
#define EDGE_EVT_DLMS_BASE 0x1100u
#define EDGE_EVT_RELAY_BASE 0x1200u

#define EDGE_EVT_UART0_RX (EDGE_EVT_BOARD_BASE + 0x01u)
#define EDGE_EVT_UART0_TX_DONE (EDGE_EVT_BOARD_BASE + 0x02u)
#define EDGE_EVT_DLT645_RX (EDGE_EVT_DLT645_BASE + 0x01u)
#define EDGE_EVT_DLMS_RX (EDGE_EVT_DLMS_BASE + 0x01u)
#define EDGE_EVT_RELAY_CHANGED (EDGE_EVT_RELAY_BASE + 0x01u)

_Static_assert(EDGE_EVT_UART0_RX != EDGE_EVT_UART0_TX_DONE, "duplicate event ID");
_Static_assert(EDGE_EVT_UART0_RX != EDGE_EVT_DLT645_RX, "duplicate event ID");
_Static_assert(EDGE_EVT_UART0_RX != EDGE_EVT_DLMS_RX, "duplicate event ID");
_Static_assert(EDGE_EVT_UART0_RX != EDGE_EVT_RELAY_CHANGED, "duplicate event ID");
_Static_assert(EDGE_EVT_UART0_TX_DONE != EDGE_EVT_DLT645_RX, "duplicate event ID");
_Static_assert(EDGE_EVT_UART0_TX_DONE != EDGE_EVT_DLMS_RX, "duplicate event ID");
_Static_assert(EDGE_EVT_UART0_TX_DONE != EDGE_EVT_RELAY_CHANGED, "duplicate event ID");
_Static_assert(EDGE_EVT_DLT645_RX != EDGE_EVT_DLMS_RX, "duplicate event ID");
_Static_assert(EDGE_EVT_DLT645_RX != EDGE_EVT_RELAY_CHANGED, "duplicate event ID");
_Static_assert(EDGE_EVT_DLMS_RX != EDGE_EVT_RELAY_CHANGED, "duplicate event ID");

#endif
