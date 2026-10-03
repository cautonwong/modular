#ifndef BOARD_BLACKPILL_H
#define BOARD_BLACKPILL_H

#include "edge/event.h"
#include "soc_stm32f4/soc_stm32f4.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* WeAct BlackPill Pinout */
#define BLACKPILL_PIN_LED 13u      /* PC13 */
#define BLACKPILL_PIN_KEY 0u       /* PA0 */
#define BLACKPILL_PIN_SPI1_SCK 5u  /* PA5 */
#define BLACKPILL_PIN_SPI1_MISO 6u /* PA6 */
#define BLACKPILL_PIN_SPI1_MOSI 7u /* PA7 */
#define BLACKPILL_PIN_I2C1_SCL 6u  /* PB6 */
#define BLACKPILL_PIN_I2C1_SDA 7u  /* PB7 */

/* BlackPill Board Hardware Registers Block */
typedef struct board_blackpill_hw {
    soc_stm32f4_gpio_regs_t gpioa;
    soc_stm32f4_gpio_regs_t gpiob;
    soc_stm32f4_gpio_regs_t gpioc;
    soc_stm32f4_rcc_regs_t rcc;
    soc_stm32f4_iwdg_regs_t iwdg;
    soc_stm32f4_spi_regs_t spi1;
    soc_stm32f4_i2c_regs_t i2c1;
} board_blackpill_hw_t;

/* Board Lifecycle & Controls */
void board_blackpill_init(edge_event_sink_t *sink);
bool board_blackpill_hw_init(board_blackpill_hw_t *hw);
void board_blackpill_set_led(bool on);
void board_blackpill_irq_key(bool pressed);
void board_blackpill_irq_matrix(uint8_t row, uint8_t col, bool pressed);

void board_blackpill_enter_low_power(void);
void board_blackpill_feed_watchdog(void);
void board_blackpill_system_reset(void);

uint32_t board_blackpill_low_power_entries(void);
uint32_t board_blackpill_watchdog_feeds(void);
uint32_t board_blackpill_reset_count(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_BLACKPILL_H */
