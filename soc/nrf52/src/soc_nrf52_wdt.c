#include "soc_nrf52/soc_nrf52.h"

bool soc_nrf52_wdt_init(soc_nrf52_wdt_regs_t *wdt, uint32_t timeout_seconds) {
    if (wdt == NULL || timeout_seconds == 0u) {
        return false;
    }
    /* Timeout = (CRV + 1) / 32768 */
    uint32_t crv = timeout_seconds * 32768u - 1u;
    wdt->crv = crv;
    wdt->config = 0x01u; /* Run during sleep */
    wdt->rren = 0x01u;   /* Enable RR[0] */
    wdt->tasks_start = 1u;
    return true;
}

void soc_nrf52_wdt_feed(soc_nrf52_wdt_regs_t *wdt) {
    if (wdt != NULL) {
        wdt->rr[0] = SOC_NRF52_WDT_RR_VALUE;
    }
}
