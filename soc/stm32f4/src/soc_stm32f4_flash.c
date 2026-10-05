#include "soc_stm32f4/soc_stm32f4.h"

/*
 * RM0090 3.8.2: the interface opens when the two keys are written in order, and the hardware clears
 * its own lock bit once the second one is right. This does both, because on a model the register
 * write alone would leave the interface looking locked to everything that follows.
 */
bool soc_stm32f4_flash_unlock(soc_stm32f4_flash_regs_t *flash) {
    if (flash == (void *)0) {
        return false;
    }
    if ((flash->cr & SOC_FLASH_CR_LOCK) == 0u) {
        return true;
    }

    flash->keyr = SOC_FLASH_KEY1;
    flash->keyr = SOC_FLASH_KEY2;
    flash->cr &= ~SOC_FLASH_CR_LOCK;
    return true;
}

void soc_stm32f4_flash_lock(soc_stm32f4_flash_regs_t *flash) {
    if (flash == (void *)0) {
        return;
    }
    flash->cr |= SOC_FLASH_CR_LOCK;
}

/*
 * The wait every operation in the manual's sequence asks for (RM0090 3.3.3): until BSY clears. The
 * hardware always clears it; a model that never did would spin here for ever, so the wait is
 * bounded, and a bound that runs out is a failure the caller is told about rather than a hang.
 */
static bool flash_wait_not_busy(const soc_stm32f4_flash_regs_t *flash) {
    for (uint32_t i = 0u; i < 1000000u; ++i) {
        if ((flash->sr & SOC_FLASH_SR_BSY) == 0u) {
            return true;
        }
    }
    return false;
}

/*
 * The end-of-operation bit and the errors are cleared by writing them back (RM0090 3.8.5); the busy
 * flag is the controller's own and a write cannot touch it. What this does is the effect of that
 * write on a cell rather than the write itself, so that the busy flag a port reports survives it.
 */
static void flash_clear_status(soc_stm32f4_flash_regs_t *flash) {
    flash->sr &= ~(SOC_FLASH_SR_EOP | SOC_FLASH_SR_ERRORS);
}

bool soc_stm32f4_flash_erase_sector(soc_stm32f4_flash_regs_t *flash, uint32_t sector,
                                    uint32_t *status) {
    if (flash == (void *)0 || (flash->cr & SOC_FLASH_CR_LOCK) != 0u) {
        return false;
    }

    flash_clear_status(flash);

    /* The bit and its number together, then the start, then the wait (RM0090 3.3.3). */
    flash->cr &= ~SOC_FLASH_CR_SNB_MASK;
    flash->cr |= SOC_FLASH_CR_SER | ((sector << SOC_FLASH_CR_SNB_SHIFT) & SOC_FLASH_CR_SNB_MASK);
    flash->cr |= SOC_FLASH_CR_STRT;

    const bool waited = flash_wait_not_busy(flash);
    /* The start bit clears itself once the controller takes it (RM0090 3.8.4), which is part of
     * what the hardware does; the model has to clear it too or a run that followed the manual would
     * leave it raised for ever. */
    flash->cr &= ~(SOC_FLASH_CR_SER | SOC_FLASH_CR_STRT);

    /*
     * What the status register held when the wait ended is what the caller is told, errors
     * included; the reference's own callers are what test it.
     */
    if (status != (void *)0) {
        *status = flash->sr;
    }

    const bool ok = waited && ((flash->sr & SOC_FLASH_SR_ERRORS) == 0u);
    flash_clear_status(flash);
    return ok;
}

bool soc_stm32f4_flash_program_word(soc_stm32f4_flash_regs_t *flash, volatile uint32_t *address,
                                    uint32_t value, uint32_t *status) {
    if (flash == (void *)0 || address == (void *)0 || (flash->cr & SOC_FLASH_CR_LOCK) != 0u) {
        return false;
    }

    flash_clear_status(flash);

    /* The programming bit, the half-word write, the wait, and the bit cleared again (RM0090 3.3.4).
     * A word is two half-words to the controller, which is why the manual's own sequence writes the
     * low half first; the model below stores what it is given. */
    flash->cr |= SOC_FLASH_CR_PG;
    *address = value;
    const bool waited = flash_wait_not_busy(flash);
    flash->cr &= ~SOC_FLASH_CR_PG;

    if (status != (void *)0) {
        *status = flash->sr;
    }

    const bool ok = waited && ((flash->sr & SOC_FLASH_SR_ERRORS) == 0u);
    flash_clear_status(flash);
    return ok;
}
