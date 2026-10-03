#ifndef BOARD_NICE_NANO_H
#define BOARD_NICE_NANO_H

#include "edge/event.h"
#include "soc_nrf52/soc_nrf52.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* nice!nano Pinout (nRF52840 Pro Micro footprint) */
#define NICE_NANO_PIN_VBATT 4u         /* AIN2 / P0.04 */
#define NICE_NANO_PIN_LED_BLUE 15u     /* P0.15 */
#define NICE_NANO_PIN_LED_CHARGING 41u /* P1.09 */
#define NICE_NANO_PIN_EXT_POWER 13u    /* P0.13 */

/* nice!nano Board Hardware Registers Block */
typedef struct board_nice_nano_hw {
    soc_nrf52_gpiote_regs_t gpiote;
    soc_nrf52_gpio_regs_t gpio;
    soc_nrf52_saadc_regs_t saadc_battery;
    soc_nrf52_rtc_regs_t rtc_clock;
    soc_nrf52_wdt_regs_t wdt_watchdog;
} board_nice_nano_hw_t;

/* Board Lifecycle & Controls */
void board_nice_nano_init(edge_event_sink_t *sink);
bool board_nice_nano_hw_init(board_nice_nano_hw_t *hw);
void board_nice_nano_set_led(bool on);
void board_nice_nano_set_ext_power(bool enable);
void board_nice_nano_irq_matrix(uint8_t row, uint8_t col, bool pressed);

void board_nice_nano_enter_low_power(void);
void board_nice_nano_feed_watchdog(void);
void board_nice_nano_system_reset(void);

uint32_t board_nice_nano_low_power_entries(void);
uint32_t board_nice_nano_watchdog_feeds(void);
uint32_t board_nice_nano_reset_count(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_NICE_NANO_H */
