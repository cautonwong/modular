#include "soc_nrf52/soc_nrf52.h"
#include <stddef.h>
#include <stdint.h>

#define NVMC_CONFIG_REN 0x00u
#define NVMC_CONFIG_WEN 0x01u
#define NVMC_CONFIG_EEN 0x02u

bool soc_nrf52_nvmc_erase_page(soc_nrf52_nvmc_regs_t *nvmc, uint32_t page_addr) {
    if (nvmc == NULL || (page_addr & 0xFFFu) != 0u) {
        return false; /* Must be 4KB page aligned */
    }
    /* Enable erase */
    nvmc->config = NVMC_CONFIG_EEN;
    /* Trigger page erase */
    nvmc->erasepage = page_addr;
    /* Wait until ready */
    nvmc->ready = 1u;
    /* Restore read-only mode */
    nvmc->config = NVMC_CONFIG_REN;
    return true;
}

bool soc_nrf52_nvmc_write_word(soc_nrf52_nvmc_regs_t *nvmc, uintptr_t dest_addr, uint32_t val) {
    if (nvmc == NULL || (dest_addr & 0x3u) != 0u) {
        return false; /* Must be 4-byte word aligned */
    }
    /* Enable write */
    nvmc->config = NVMC_CONFIG_WEN;
    /* Write word */
    *(volatile uint32_t *)dest_addr = val;
    /* Wait until ready */
    nvmc->ready = 1u;
    /* Restore read-only mode */
    nvmc->config = NVMC_CONFIG_REN;
    return true;
}

bool soc_nrf52_nvmc_write_words(soc_nrf52_nvmc_regs_t *nvmc, uintptr_t dest_addr,
                                const uint32_t *src, size_t num_words) {
    if (nvmc == NULL || src == NULL || (dest_addr & 0x3u) != 0u) {
        return false;
    }
    nvmc->config = NVMC_CONFIG_WEN;
    volatile uint32_t *dest = (volatile uint32_t *)dest_addr;
    for (size_t i = 0u; i < num_words; ++i) {
        dest[i] = src[i];
    }
    nvmc->ready = 1u;
    nvmc->config = NVMC_CONFIG_REN;
    return true;
}
