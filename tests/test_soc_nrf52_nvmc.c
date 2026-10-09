/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "soc_nrf52/soc_nrf52.h"

static void test_soc_nrf52_nvmc_layout(void **state) {
    (void)state;
    soc_nrf52_nvmc_regs_t nvmc = {0};
    assert_int_equal(sizeof(soc_nrf52_nvmc_regs_t), 0x518u);
    assert_true(soc_nrf52_nvmc_erase_page(&nvmc, 0x1000u));
    assert_int_equal(nvmc.config, 0x00u); /* Returned to Ren */
    assert_int_equal(nvmc.erasepage, 0x1000u);

    /* Misaligned page addr fails */
    assert_false(soc_nrf52_nvmc_erase_page(&nvmc, 0x1004u));
}

static void test_soc_nrf52_nvmc_write(void **state) {
    (void)state;
    soc_nrf52_nvmc_regs_t nvmc = {0};
    uint32_t ram_target = 0u;

    assert_true(soc_nrf52_nvmc_write_word(&nvmc, (uintptr_t)&ram_target, 0xDEADBEEFu));
    assert_int_equal(ram_target, 0xDEADBEEFu);
    assert_int_equal(nvmc.config, 0x00u);

    uint32_t ram_array[3] = {0};
    const uint32_t data[3] = {1, 2, 3};
    assert_true(soc_nrf52_nvmc_write_words(&nvmc, (uintptr_t)ram_array, data, 3u));
    assert_int_equal(ram_array[0], 1u);
    assert_int_equal(ram_array[1], 2u);
    assert_int_equal(ram_array[2], 3u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_soc_nrf52_nvmc_layout),
        cmocka_unit_test(test_soc_nrf52_nvmc_write),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
