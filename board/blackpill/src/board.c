#include "blackpill/board.h"

#include "edge/events.h"
#include <stddef.h>

static uint32_t g_low_power_entries;
static uint32_t g_watchdog_feeds;
static uint32_t g_reset_count;
static bool g_led_state;
static edge_event_sink_t *g_event_sink;
static board_blackpill_hw_t *g_hw_inst;

void board_blackpill_init(edge_event_sink_t *sink) {
    g_event_sink = sink;
    g_low_power_entries = 0u;
    g_watchdog_feeds = 0u;
    g_reset_count = 0u;
    g_led_state = false;
    g_hw_inst = NULL;
}

bool board_blackpill_hw_init(board_blackpill_hw_t *hw) {
    if (hw == NULL) {
        return false;
    }
    g_hw_inst = hw;
    *hw = (board_blackpill_hw_t){0};

    /* Enable clock for GPIOA, GPIOB, GPIOC */
    hw->rcc.ahb1enr |= (1u << 0u) | (1u << 1u) | (1u << 2u);

    /* PC13 Output (User LED) */
    soc_stm32f4_gpio_set_mode(&hw->gpioc, BLACKPILL_PIN_LED, SOC_STM32F4_GPIO_MODE_OUTPUT);

    /* PA0 Input (User Button) */
    soc_stm32f4_gpio_set_mode(&hw->gpioa, BLACKPILL_PIN_KEY, SOC_STM32F4_GPIO_MODE_INPUT);

    /* SPI1 Init */
    (void)soc_stm32f4_spi_init(&hw->spi1, 0x0004u);

    /* I2C1 Init */
    (void)soc_stm32f4_i2c_init(&hw->i2c1, 0x0028u);

    /* Watchdog Init */
    soc_stm32f4_iwdg_start(&hw->iwdg, 2000u);

    return true;
}

void board_blackpill_set_led(bool on) {
    g_led_state = on;
    if (g_hw_inst != NULL) {
        /* PC13 is active low */
        soc_stm32f4_gpio_write(&g_hw_inst->gpioc, BLACKPILL_PIN_LED, !on);
    }
}

void board_blackpill_enter_low_power(void) {
    ++g_low_power_entries;
}

void board_blackpill_feed_watchdog(void) {
    ++g_watchdog_feeds;
    if (g_hw_inst != NULL) {
        soc_stm32f4_iwdg_feed(&g_hw_inst->iwdg);
    }
}

void board_blackpill_system_reset(void) {
    ++g_reset_count;
}

uint32_t board_blackpill_low_power_entries(void) {
    return g_low_power_entries;
}

uint32_t board_blackpill_watchdog_feeds(void) {
    return g_watchdog_feeds;
}

uint32_t board_blackpill_reset_count(void) {
    return g_reset_count;
}

void board_blackpill_irq_key(bool pressed) {
    if (g_event_sink == NULL) {
        return;
    }
    const edge_event_t evt = {
        .id = EDGE_EVT_WATCH_BUTTON,
        .source = 0u,
        .arg0 = pressed ? 1u : 0u,
        .arg1 = 0u,
        .timestamp = 0u,
    };
    (void)edge_event_sink_push_isr(g_event_sink, &evt);
}

void board_blackpill_irq_matrix(uint8_t row, uint8_t col, bool pressed) {
    if (g_event_sink == NULL) {
        return;
    }
    const edge_event_t evt = {
        .id = EDGE_EVT_ZMK_POSITION_STATE_CHANGED,
        .source = 0u,
        .arg0 = ((uint32_t)row << 16u) | (uint32_t)col,
        .arg1 = pressed ? 1u : 0u,
        .timestamp = 0u,
    };
    (void)edge_event_sink_push_isr(g_event_sink, &evt);
}
