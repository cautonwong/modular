#ifndef BOARD_PINETIME_H
#define BOARD_PINETIME_H

#include "edge/event.h"
#include "soc_nrf52/soc_nrf52.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LCD ST7789 Pin definitions */
#define PINETIME_PIN_LCD_SCK 2u
#define PINETIME_PIN_LCD_MOSI 3u
#define PINETIME_PIN_LCD_CS 25u
#define PINETIME_PIN_LCD_DC 18u
#define PINETIME_PIN_LCD_RST 26u
#define PINETIME_PIN_LCD_BACKLIGHT_LOW 14u
#define PINETIME_PIN_LCD_BACKLIGHT_MID 22u
#define PINETIME_PIN_LCD_BACKLIGHT_HIGH 23u

/* CST816S Touch screen Pin definitions */
#define PINETIME_PIN_TOUCH_SDA 6u
#define PINETIME_PIN_TOUCH_SCL 7u
#define PINETIME_PIN_TOUCH_INT 28u
#define PINETIME_PIN_TOUCH_RST 10u

/* BMA421 IMU Pin definitions */
#define PINETIME_PIN_IMU_INT 8u

/* SPI Flash XT25F32B Pin definitions */
#define PINETIME_PIN_FLASH_CS 5u

/* User Button & Vibration Motor */
#define PINETIME_PIN_BUTTON 13u
#define PINETIME_PIN_MOTOR 16u

/* Battery & Power Management */
#define PINETIME_PIN_BATTERY_ADC 31u
#define PINETIME_PIN_CHARGING 12u
#define PINETIME_PIN_POWER_PRESENT 19u

/* Pinetime Board Hardware Registers Block */
typedef struct board_pinetime_hw {
    soc_nrf52_gpiote_regs_t gpiote;
    soc_nrf52_gpio_regs_t gpio;
    soc_nrf52_spim_regs_t spim_lcd;
    soc_nrf52_spim_regs_t spim_flash;
    soc_nrf52_twim_regs_t twim_sensors;
    soc_nrf52_saadc_regs_t saadc_battery;
    soc_nrf52_rtc_regs_t rtc_clock;
    soc_nrf52_wdt_regs_t wdt_watchdog;
} board_pinetime_hw_t;

/* Board lifecycle & IRQ capture */
void board_pinetime_init(edge_event_sink_t *sink);
bool board_pinetime_hw_init(board_pinetime_hw_t *hw);
void board_pinetime_irq_button(bool pressed);
void board_pinetime_irq_touch(void);
void board_pinetime_irq_imu(void);

void board_pinetime_enter_low_power(void);
void board_pinetime_feed_watchdog(void);
void board_pinetime_system_reset(void);

uint32_t board_pinetime_low_power_entries(void);
uint32_t board_pinetime_watchdog_feeds(void);
uint32_t board_pinetime_reset_count(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_PINETIME_H */
