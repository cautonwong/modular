#ifndef BOARD_RPI_PICO_H
#define BOARD_RPI_PICO_H

#include "edge/event.h"
#include "soc_rp2040/soc_rp2040.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Raspberry Pi Pico Pinout */
#define RPI_PICO_PIN_LED 25u
#define RPI_PICO_PIN_SPI0_SCK 18u
#define RPI_PICO_PIN_SPI0_TX 19u
#define RPI_PICO_PIN_SPI0_RX 16u
#define RPI_PICO_PIN_I2C0_SDA 4u
#define RPI_PICO_PIN_I2C0_SCL 5u

/* RPi Pico Board Hardware Registers Block */
typedef struct board_rpi_pico_hw {
    soc_rp2040_sio_regs_t sio;
    soc_rp2040_wdt_regs_t wdt;
    soc_rp2040_spi_regs_t spi;
    soc_rp2040_i2c_regs_t i2c;
} board_rpi_pico_hw_t;

/* Board Lifecycle & Controls */
void board_rpi_pico_init(edge_event_sink_t *sink);
bool board_rpi_pico_hw_init(board_rpi_pico_hw_t *hw);
void board_rpi_pico_set_led(bool on);
void board_rpi_pico_irq_matrix(uint8_t row, uint8_t col, bool pressed);

void board_rpi_pico_enter_low_power(void);
void board_rpi_pico_feed_watchdog(void);
void board_rpi_pico_system_reset(void);

uint32_t board_rpi_pico_low_power_entries(void);
uint32_t board_rpi_pico_watchdog_feeds(void);
uint32_t board_rpi_pico_reset_count(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_RPI_PICO_H */
