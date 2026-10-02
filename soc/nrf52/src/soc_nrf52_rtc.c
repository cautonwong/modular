#include "soc_nrf52/soc_nrf52.h"

bool soc_nrf52_rtc_init(soc_nrf52_rtc_regs_t *rtc, uint32_t prescaler) {
    if (rtc == NULL || prescaler > 0xFFFu) {
        return false;
    }
    rtc->tasks_stop = 1u;
    rtc->prescaler = prescaler;
    rtc->tasks_clear = 1u;
    rtc->counter = 0u;
    return true;
}

uint32_t soc_nrf52_rtc_get_counter(const soc_nrf52_rtc_regs_t *rtc) {
    if (rtc == NULL) {
        return 0u;
    }
    return rtc->counter & 0x00FFFFFFu;
}

void soc_nrf52_rtc_start(soc_nrf52_rtc_regs_t *rtc) {
    if (rtc != NULL) {
        rtc->tasks_start = 1u;
    }
}

void soc_nrf52_rtc_stop(soc_nrf52_rtc_regs_t *rtc) {
    if (rtc != NULL) {
        rtc->tasks_stop = 1u;
    }
}
