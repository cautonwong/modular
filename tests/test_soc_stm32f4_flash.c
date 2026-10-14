/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>

#include <cmocka.h>
/* clang-format on */

#include "edge/errors.h"
#include "soc_stm32f4/soc_stm32f4.h"

/*
 * RM0090 3.8.2's own sequence: the two keys in their order, and the lock bit that goes with the
 * second of them. An interface that is already open is left alone rather than written again.
 */
static void test_soc_stm32f4_flash_unlock_and_lock(void **state) {
    (void)state;
    soc_stm32f4_flash_regs_t flash = {0};

    flash.cr = SOC_FLASH_CR_LOCK;
    assert_true(soc_stm32f4_flash_unlock(&flash));
    assert_int_equal(flash.keyr, SOC_FLASH_KEY2);
    assert_int_equal(flash.cr & SOC_FLASH_CR_LOCK, 0u);

    flash.keyr = 0u;
    assert_true(soc_stm32f4_flash_unlock(&flash));
    assert_int_equal(flash.keyr, 0u);

    soc_stm32f4_flash_lock(&flash);
    assert_int_equal(flash.cr & SOC_FLASH_CR_LOCK, SOC_FLASH_CR_LOCK);

    /* Nothing to open. */
    assert_false(soc_stm32f4_flash_unlock(NULL));
    soc_stm32f4_flash_lock(NULL);
}

/*
 * The two operations, each in the order the manual gives them (RM0090 3.3.3 and 3.3.4): a locked
 * interface refuses them, the sector's number goes into its field and comes back out, the word
 * lands where it was told, and a wait that never ends is a failure rather than a hang.
 */
static void test_soc_stm32f4_flash_erase_and_program(void **state) {
    (void)state;
    soc_stm32f4_flash_regs_t flash = {0};
    uint32_t status = 0xFFFFFFFFu;
    uint32_t word = 0u;

    /* A locked interface refuses both rather than writing anything. */
    flash.cr = SOC_FLASH_CR_LOCK;
    assert_false(soc_stm32f4_flash_erase_sector(&flash, 3u, &status));
    assert_false(soc_stm32f4_flash_program_word(&flash, &word, 0x12345678u, &status));
    assert_int_equal(word, 0u);

    assert_true(soc_stm32f4_flash_unlock(&flash));

    assert_true(soc_stm32f4_flash_erase_sector(&flash, 3u, &status));
    assert_int_equal(status & SOC_FLASH_SR_ERRORS, 0u);
    /* The bit and its number are put back once the operation is over. */
    assert_int_equal(flash.cr & SOC_FLASH_CR_SER, 0u);
    assert_int_equal(flash.cr & SOC_FLASH_CR_STRT, 0u);
    /* And the status the operation made is cleared on the way out. */
    assert_int_equal(flash.sr & SOC_FLASH_SR_EOP, 0u);
    assert_int_equal(flash.sr & SOC_FLASH_SR_ERRORS, 0u);

    assert_true(soc_stm32f4_flash_program_word(&flash, &word, 0x12345678u, &status));
    assert_int_equal(word, 0x12345678u);
    assert_int_equal(flash.cr & SOC_FLASH_CR_PG, 0u);

    /* A sector number that does not fit its four bits is masked rather than running over. */
    assert_true(soc_stm32f4_flash_erase_sector(&flash, 0x1Fu, &status));

    /* A busy flag that never clears runs the wait out: the operation failed rather than hung. */
    flash.sr = SOC_FLASH_SR_BSY;
    assert_false(soc_stm32f4_flash_erase_sector(&flash, 1u, &status));
    assert_int_equal(status & SOC_FLASH_SR_BSY, SOC_FLASH_SR_BSY);
    flash.sr = 0u;

    /* Nothing to write into, and a status nobody asked for. */
    assert_false(soc_stm32f4_flash_program_word(&flash, NULL, 1u, &status));
    assert_false(soc_stm32f4_flash_program_word(NULL, &word, 1u, &status));
    assert_false(soc_stm32f4_flash_erase_sector(NULL, 0u, &status));
    assert_true(soc_stm32f4_flash_erase_sector(&flash, 0u, NULL));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_soc_stm32f4_flash_unlock_and_lock),
        cmocka_unit_test(test_soc_stm32f4_flash_erase_and_program),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
