#ifndef BOARD_COLMI_P8_H
#define BOARD_COLMI_P8_H

#include "edge/event.h"
#include "soc_nrf52/soc_nrf52.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Colmi P8 Pinout */
#define COLMI_P8_PIN_LCD_SCK 2u
#define COLMI_P8_PIN_LCD_MOSI 3u
#define COLMI_P8_PIN_LCD_CS 25u
#define COLMI_P8_PIN_LCD_DC 18u
#define COLMI_P8_PIN_LCD_RST 26u
#define COLMI_P8_PIN_LCD_BACKLIGHT 14u

#define COLMI_P8_PIN_TOUCH_SDA 6u
#define COLMI_P8_PIN_TOUCH_SCL 7u
#define COLMI_P8_PIN_TOUCH_INT 28u
#define COLMI_P8_PIN_TOUCH_RST 13u

#define COLMI_P8_PIN_BUTTON 17u
#define COLMI_P8_PIN_MOTOR 16u
#define COLMI_P8_PIN_CHARGING 19u
#define COLMI_P8_PIN_BATTERY_ADC 31u

/* Colmi P8 Board Hardware Registers Block */
typedef struct board_colmi_p8_hw {
    soc_nrf52_gpiote_regs_t gpiote;
    soc_nrf52_gpio_regs_t gpio;
    soc_nrf52_spim_regs_t spim_lcd;
    soc_nrf52_twim_regs_t twim_touch;
    soc_nrf52_saadc_regs_t saadc_battery;
    soc_nrf52_rtc_regs_t rtc_clock;
    soc_nrf52_wdt_regs_t wdt_watchdog;
} board_colmi_p8_hw_t;

/* Board Lifecycle & IRQs */
void board_colmi_p8_init(edge_event_sink_t *sink);
bool board_colmi_p8_hw_init(board_colmi_p8_hw_t *hw);
void board_colmi_p8_irq_button(bool pressed);
void board_colmi_p8_irq_touch(void);

void board_colmi_p8_enter_low_power(void);
void board_colmi_p8_feed_watchdog(void);
void board_colmi_p8_system_reset(void);

uint32_t board_colmi_p8_low_power_entries(void);
uint32_t board_colmi_p8_watchdog_feeds(void);
uint32_t board_colmi_p8_reset_count(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_COLMI_P8_H */
