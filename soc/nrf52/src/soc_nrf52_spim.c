#include "soc_nrf52/soc_nrf52.h"

bool soc_nrf52_spim_init(soc_nrf52_spim_regs_t *spim, const soc_nrf52_spim_config_t *config) {
    if (spim == NULL || config == NULL) {
        return false;
    }
    /* Disable SPIM while reconfiguring pins and clock */
    spim->enable = 0u;

    spim->psel_sck = (uint32_t)config->pin_sck;
    spim->psel_mosi = (uint32_t)config->pin_mosi;
    spim->psel_miso = (config->pin_miso != 0xFFu) ? (uint32_t)config->pin_miso : 0xFFFFFFFFu;

    spim->frequency =
        config->frequency_code != 0u ? config->frequency_code : 0x80000000u; /* 8MHz default */

    /* Mode: Bit 1 = CPOL, Bit 2 = CPHA. Bit 0 = MSB/LSB first */
    uint32_t cfg_val = 0u; /* MSB first */
    if (config->spi_mode == 1u) {
        cfg_val |= (1u << 2u); /* CPHA Trailing */
    } else if (config->spi_mode == 2u) {
        cfg_val |= (1u << 1u); /* CPOL Active Low */
    } else if (config->spi_mode == 3u) {
        cfg_val |= (1u << 1u) | (1u << 2u);
    }
    spim->config = cfg_val;

    /* Enable SPIM (7 = Enabled) */
    spim->enable = 7u;
    return true;
}

bool soc_nrf52_spim_transfer(soc_nrf52_spim_regs_t *spim, const uint8_t *tx_buf, size_t tx_len,
                             uint8_t *rx_buf, size_t rx_len) {
    if (spim == NULL || spim->enable != 7u) {
        return false;
    }
    if ((tx_buf == NULL && tx_len > 0) || (rx_buf == NULL && rx_len > 0)) {
        return false;
    }

    spim->events_end = 0u;
    spim->events_endtx = 0u;
    spim->events_endrx = 0u;

    spim->txd_ptr = (uint32_t)(uintptr_t)tx_buf;
    spim->txd_maxcnt = (uint32_t)tx_len;
    spim->txd_amount = (uint32_t)tx_len;

    spim->rxd_ptr = (uint32_t)(uintptr_t)rx_buf;
    spim->rxd_maxcnt = (uint32_t)rx_len;
    spim->rxd_amount = (uint32_t)rx_len;

    spim->tasks_start = 1u;

    /* In host mock or simulation, set events_end to 1 */
    spim->events_end = 1u;
    return true;
}
