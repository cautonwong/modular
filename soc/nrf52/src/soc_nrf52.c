#include "soc_nrf52/soc_nrf52.h"

bool soc_nrf52_gpiote_config_input_event(soc_nrf52_gpiote_regs_t *gpiote, uint8_t channel,
                                         uint8_t pin, soc_nrf52_gpiote_polarity_t polarity) {
    if (gpiote == NULL || channel >= 8u || pin >= 32u) {
        return false;
    }
    /* Mode 1 = Event mode, Pin [8..12], Polarity [16..17] */
    uint32_t val = 1u | ((uint32_t)pin << 8u) | ((uint32_t)polarity << 16u);
    gpiote->config[channel] = val;
    gpiote->intenset = (1u << channel);
    return true;
}
