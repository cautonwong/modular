#include "soc_nrf52/soc_nrf52.h"

bool soc_nrf52_saadc_init(soc_nrf52_saadc_regs_t *saadc, const soc_nrf52_saadc_config_t *config) {
    if (saadc == NULL || config == NULL || config->channel >= 8u) {
        return false;
    }
    saadc->enable = 0u;

    /* PSELP: 1 + pin (AIN0 is 1, AIN7 is 8) */
    saadc->ch[config->channel].pselp = (uint32_t)(config->ain_pin + 1u);
    saadc->ch[config->channel].pseln = 0u; /* NC (Single ended) */
    saadc->ch[config->channel].config = config->gain_and_ref != 0u
                                            ? config->gain_and_ref
                                            : 0x00020000u; /* Gain 1/6, Internal ref 0.6V */

    saadc->resolution = (uint32_t)config->resolution; /* 0=8b, 1=10b, 2=12b, 3=14b */
    saadc->enable = 1u;
    return true;
}

int16_t soc_nrf52_saadc_sample_blocking(soc_nrf52_saadc_regs_t *saadc, int16_t *out_val) {
    if (saadc == NULL || saadc->enable != 1u) {
        return -1;
    }
    saadc->events_started = 0u;
    saadc->events_done = 0u;
    saadc->events_end = 0u;

    saadc->tasks_start = 1u;
    saadc->events_started = 1u;

    saadc->tasks_sample = 1u;
    saadc->events_done = 1u;
    saadc->events_end = 1u;

    if (out_val != NULL) {
        *out_val = 650; /* Mock 650 counts = ~3.9V with divider */
    }
    return 0;
}
