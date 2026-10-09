#include "soc_nrf52/soc_nrf52.h"

bool soc_nrf52_twim_init(soc_nrf52_twim_regs_t *twim, const soc_nrf52_twim_config_t *config) {
    if (twim == NULL || config == NULL) {
        return false;
    }
    twim->enable = 0u;

    twim->psel_scl = (uint32_t)config->pin_scl;
    twim->psel_sda = (uint32_t)config->pin_sda;
    twim->frequency =
        config->frequency_code != 0u ? config->frequency_code : 0x06680000u; /* 400kHz default */

    twim->enable = 6u; /* 6 = TWIM Enabled */
    return true;
}

bool soc_nrf52_twim_write(soc_nrf52_twim_regs_t *twim, uint8_t addr, const uint8_t *tx_buf,
                          size_t tx_len, bool no_stop) {
    (void)no_stop;
    if (twim == NULL || twim->enable != 6u) {
        return false;
    }
    if (tx_buf == NULL && tx_len > 0) {
        return false;
    }

    twim->address = (uint32_t)addr;
    twim->txd_ptr = (uint32_t)(uintptr_t)tx_buf;
    twim->txd_maxcnt = (uint32_t)tx_len;
    twim->txd_amount = (uint32_t)tx_len;

    twim->events_lasttx = 0u;
    twim->events_stopped = 0u;
    twim->tasks_starttx = 1u;

    twim->events_lasttx = 1u;
    twim->events_stopped = 1u;
    return true;
}

bool soc_nrf52_twim_read(soc_nrf52_twim_regs_t *twim, uint8_t addr, uint8_t *rx_buf,
                         size_t rx_len) {
    if (twim == NULL || twim->enable != 6u) {
        return false;
    }
    if (rx_buf == NULL && rx_len > 0) {
        return false;
    }

    twim->address = (uint32_t)addr;
    twim->rxd_ptr = (uint32_t)(uintptr_t)rx_buf;
    twim->rxd_maxcnt = (uint32_t)rx_len;
    twim->rxd_amount = (uint32_t)rx_len;

    twim->events_lastrx = 0u;
    twim->events_stopped = 0u;
    twim->tasks_startrx = 1u;

    twim->events_lastrx = 1u;
    twim->events_stopped = 1u;
    return true;
}
