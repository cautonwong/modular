/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>

#include <cmocka.h>
/* clang-format on */

#include "app_select/app_select.h"

/*
 * applications/app.c:91-…'s own table, which the reference's dispatcher switches on: every
 * configured application is the parts it starts for it - up to two - the two arguments two of them
 * are told, and the servo decoder's own rule.
 */
static void test_app_plan_matches_the_reference(void **state) {
    (void)state;

    /* One part each, and the two that are told the serial port is theirs. */
    app_plan_t p = app_plan_for(APP_USE_PPM, false);
    assert_int_equal(p.parts, APP_PART_PPM);
    assert_false(p.adc_own_uart);
    assert_false(p.pas_own_uart);

    p = app_plan_for(APP_USE_ADC, false);
    assert_int_equal(p.parts, APP_PART_ADC);
    assert_true(p.adc_own_uart);

    assert_int_equal(app_plan_for(APP_USE_UART, false).parts, APP_PART_UART_COMM);

    /* The three applications that are two parts at once, and what the second part is told. */
    assert_int_equal(app_plan_for(APP_USE_PPM_UART, false).parts,
                     APP_PART_PPM | APP_PART_UART_COMM);

    p = app_plan_for(APP_USE_ADC_UART, false);
    assert_int_equal(p.parts, APP_PART_ADC | APP_PART_UART_COMM);
    assert_false(p.adc_own_uart);

    p = app_plan_for(APP_USE_ADC_PAS, false);
    assert_int_equal(p.parts, APP_PART_ADC | APP_PART_PAS);
    assert_false(p.pas_own_uart);

    /* The rest of the table's own cases. */
    assert_int_equal(app_plan_for(APP_USE_NUNCHUK, false).parts, APP_PART_NUNCHUK);
    assert_int_equal(app_plan_for(APP_USE_NRF, false).parts, APP_PART_NRF);
    assert_int_equal(app_plan_for(APP_USE_CUSTOM, false).parts, APP_PART_CUSTOM);

    p = app_plan_for(APP_USE_PAS, false);
    assert_int_equal(p.parts, APP_PART_PAS);
    assert_true(p.pas_own_uart);

    /* Nothing configured is nothing to start, and so is anything the table does not name. */
    assert_int_equal(app_plan_for(APP_USE_NONE, false).parts, 0u);
    assert_int_equal(app_plan_for(99u, false).parts, 0u);

    /* The servo decoder runs in place of the PPM input and only then: never for either of the two
     * PPM applications, and only while the servo output is enabled. */
    assert_false(app_plan_for(APP_USE_PPM, true).servo_decoder);
    assert_false(app_plan_for(APP_USE_PPM_UART, true).servo_decoder);
    assert_true(app_plan_for(APP_USE_ADC, true).servo_decoder);
    assert_false(app_plan_for(APP_USE_ADC, false).servo_decoder);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_app_plan_matches_the_reference),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
