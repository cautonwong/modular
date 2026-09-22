#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "riscv_virt/board.h"

/*
 * The MTIMECMP arithmetic (#164), host-tested because the boundary it guards is
 * unreachable in CI: riscv-virt's 64-bit `mtime` passes 2^32 after ~429 s at
 * 10 MHz, so a smoke that runs for seconds always sees a zero high word - which is
 * exactly why `hi = 0` looked correct and fails in the field.
 */

static void test_no_carry_inside_the_low_word(void **state) {
    (void)state;
    uint32_t lo = 0u;
    uint32_t hi = 0u;
    riscv_virt_deadline(1000u, 0u, 500u, &lo, &hi);
    assert_int_equal(lo, 1500u);
    assert_int_equal(hi, 0u);
}

static void test_carry_crosses_into_the_high_word(void **state) {
    (void)state;
    uint32_t lo = 0u;
    uint32_t hi = 0u;
    riscv_virt_deadline(0xFFFFFFF0u, 7u, 0x20u, &lo, &hi);
    assert_int_equal(lo, 0x10u);
    assert_int_equal(hi, 8u); /* without the carry the compare value is in the past */
}

static void test_a_nonzero_high_word_is_preserved(void **state) {
    (void)state;
    uint32_t lo = 0u;
    uint32_t hi = 0u;
    riscv_virt_deadline(5u, 42u, 100u, &lo, &hi);
    assert_int_equal(lo, 105u);
    assert_int_equal(hi, 42u); /* dropping this is the defect: the timer fires at once */
}

static void test_zero_delta_is_the_current_count(void **state) {
    (void)state;
    uint32_t lo = 1u;
    uint32_t hi = 1u;
    riscv_virt_deadline(1u, 1u, 0u, &lo, &hi);
    assert_int_equal(lo, 1u);
    assert_int_equal(hi, 1u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_no_carry_inside_the_low_word),
        cmocka_unit_test(test_carry_crosses_into_the_high_word),
        cmocka_unit_test(test_a_nonzero_high_word_is_preserved),
        cmocka_unit_test(test_zero_delta_is_the_current_count),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
