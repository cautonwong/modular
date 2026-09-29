/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "soc_stm32f4/soc_stm32f4.h"

/* The ADC block is a block of RAM, laid out as the header asserts against the manual. */
static soc_stm32f4_adc_regs_t adc;

/* The reference's own first ADC: channel 10, three injected ranks, 15-cycle sample time, and the
 * end-of-injected-conversion interrupt on (hw_60_core.c:174, :205-213). */
static const soc_stm32f4_adc_config_t vesc6_ish = {
    .channel = 10u,
    .samples = 3u,
    .sample_time = 1u,
    .jeoc_interrupt = true,
};

static uint32_t hook_calls;
static uint32_t hook_index;
static void *hook_ctx_seen;

static void injected_hook(void *ctx, uint32_t adc_index) {
    ++hook_calls;
    hook_index = adc_index;
    hook_ctx_seen = ctx;
}

static void test_soc_stm32f4_adc_injected_setup(void **state) {
    (void)state;
    memset(&adc, 0, sizeof(adc));

    /* The ranks programmed come back, or zero when the configuration is refused. */
    assert_int_equal(soc_stm32f4_adc_init_injected(&adc, &vesc6_ish), 3u);

    /* CR1: ADON (bit 0) and the interrupt (bit 7). */
    assert_int_equal(adc.cr1, 0x81u);
    /* CR2: JEXTSEL = 1 (TIM1_TRGO) at 19:16, JEXTEN = 01 (rising edge) at 21:20. */
    assert_int_equal(adc.cr2, 0x110000u);
    /* JSQR: one channel at each of three ranks, JL = 2 for three conversions. */
    assert_int_equal(adc.jsqr, (2u << 20) | (10u << 15) | (10u << 10) | (10u << 5));
    /* The sample time for channel 10 is field 0 of SMPR1, and 1 is the 15 cycles asked for. */
    assert_int_equal(adc.smpr1, 0x1u);
    assert_int_equal(adc.smpr2, 0u);

    /* Without the interrupt asked for, the bit stays clear and nothing else changes. */
    soc_stm32f4_adc_config_t quiet = vesc6_ish;
    quiet.jeoc_interrupt = false;
    memset(&adc, 0, sizeof(adc));
    assert_int_equal(soc_stm32f4_adc_init_injected(&adc, &quiet), 3u);
    assert_int_equal(adc.cr1, 0x1u);

    /* A channel below 10 takes its sample time from SMPR2, three bits in. */
    soc_stm32f4_adc_config_t low_channel = vesc6_ish;
    low_channel.channel = 5u;
    memset(&adc, 0, sizeof(adc));
    assert_int_equal(soc_stm32f4_adc_init_injected(&adc, &low_channel), 3u);
    assert_int_equal(adc.smpr1, 0u);
    assert_int_equal(adc.smpr2, 1u << 15);
}

static void test_soc_stm32f4_adc_reads_the_ranks(void **state) {
    (void)state;
    memset(&adc, 0, sizeof(adc));
    assert_int_equal(soc_stm32f4_adc_init_injected(&adc, &vesc6_ish), 3u);

    adc.jdr1 = 0x1111u;
    adc.jdr2 = 0x2222u;
    adc.jdr3 = 0x3333u;
    adc.jdr4 = 0x4444u;

    uint16_t out[4] = {0xFFFFu, 0xFFFFu, 0xFFFFu, 0xFFFFu};
    assert_true(soc_stm32f4_adc_read_injected(&adc, 3u, out));
    assert_int_equal(out[0], 0x1111u);
    assert_int_equal(out[1], 0x2222u);
    assert_int_equal(out[2], 0x3333u);
    assert_int_equal(out[3], 0xFFFFu); /* only three ranks were asked for */

    assert_true(soc_stm32f4_adc_read_injected(&adc, 4u, out));
    assert_int_equal(out[3], 0x4444u);

    assert_false(soc_stm32f4_adc_read_injected(&adc, 0u, out));
    assert_false(soc_stm32f4_adc_read_injected(&adc, 5u, out));
    assert_false(soc_stm32f4_adc_read_injected(NULL, 1u, out));
    assert_false(soc_stm32f4_adc_read_injected(&adc, 1u, NULL));
}

static void test_soc_stm32f4_adc_isr_forwards(void **state) {
    (void)state;
    memset(&adc, 0, sizeof(adc));
    hook_calls = 0u;
    hook_index = 99u;
    hook_ctx_seen = NULL;
    int marker = 0;
    soc_stm32f4_adc_set_injected_handler(injected_hook, &marker);

    /* Nothing pending: the hook is not called. */
    soc_stm32f4_adc_service_injected_isr(&adc, 1u);
    assert_int_equal(hook_calls, 0u);
    soc_stm32f4_adc_service_injected_isr(NULL, 1u);
    assert_int_equal(hook_calls, 0u);

    /* JEOC set: the hook runs, is told which ADC, and the flag is written back as zero. The ADC's
     * status bits are cleared by writing zero to them (RM0090 13.13.1), so the write carries ones
     * everywhere else - on the part those are no-ops. */
    adc.sr = 0x04u;
    soc_stm32f4_adc_service_injected_isr(&adc, 2u);
    assert_int_equal(hook_calls, 1u);
    assert_int_equal(hook_index, 2u);
    assert_ptr_equal(hook_ctx_seen, &marker);
    assert_int_equal(adc.sr, ~0x04u);

    /* A flag other than JEOC does not call the hook. */
    adc.sr = 0x20u;
    soc_stm32f4_adc_service_injected_isr(&adc, 3u);
    assert_int_equal(hook_calls, 1u);
}

static void test_soc_stm32f4_adc_refuses_impossible_configs(void **state) {
    (void)state;
    memset(&adc, 0, sizeof(adc));

    assert_int_equal(soc_stm32f4_adc_init_injected(NULL, &vesc6_ish), 0u);
    assert_int_equal(soc_stm32f4_adc_init_injected(&adc, NULL), 0u);

    soc_stm32f4_adc_config_t bad = vesc6_ish;
    bad.samples = 0u;
    assert_int_equal(soc_stm32f4_adc_init_injected(&adc, &bad), 0u);
    bad = vesc6_ish;
    bad.samples = 5u;
    assert_int_equal(soc_stm32f4_adc_init_injected(&adc, &bad), 0u);
    bad = vesc6_ish;
    bad.sample_time = 8u;
    assert_int_equal(soc_stm32f4_adc_init_injected(&adc, &bad), 0u);
    bad = vesc6_ish;
    bad.channel = 18u;
    assert_int_equal(soc_stm32f4_adc_init_injected(&adc, &bad), 0u);

    /* Nothing was written by any of them. */
    assert_int_equal(adc.cr1, 0u);
    assert_int_equal(adc.cr2, 0u);
    assert_int_equal(adc.jsqr, 0u);

    /* One rank is a legal configuration. */
    soc_stm32f4_adc_config_t single = vesc6_ish;
    single.samples = 1u;
    assert_int_equal(soc_stm32f4_adc_init_injected(&adc, &single), 1u);
    assert_int_equal(adc.jsqr, 10u << 15);
}

static void test_soc_stm32f4_adc_regular_conversion(void **state) {
    (void)state;
    memset(&adc, 0, sizeof(adc));
    soc_stm32f4_adc_config_t injected = vesc6_ish;
    injected.jeoc_interrupt = false;
    assert_int_equal(soc_stm32f4_adc_init_injected(&adc, &injected), 3u);

    /* The supply voltage's channel, on the regular sequence: a length of one, the channel in the
     * first rank of SQR3, and the external trigger left off because the read starts it. */
    assert_int_equal(soc_stm32f4_adc_init_regular(&adc, 11u, 1u), 1u);
    assert_int_equal(adc.sqr1, 0u);
    assert_int_equal(adc.sqr3, 11u);
    assert_int_equal((adc.cr2 & 0x30000000u), 0u);
    /* Channel 11 is field 1 of SMPR1, i.e. three bits in, so field 0 (10) and field 1 (11) both
     * carry the 15-cycle setting. */
    assert_int_equal(adc.smpr1, 0x9u);

    /* A conversion: the read starts it, takes the data register, and clears the flag it waited
     * for. The status bits are cleared by writing zero to them, so the write carries ones
     * elsewhere. */
    adc.dr = 0x0ABCu;
    adc.sr = 0x02u;
    uint16_t value = 0u;
    assert_true(soc_stm32f4_adc_read_regular(&adc, &value));
    assert_int_equal(value, 0x0ABCu);
    assert_int_equal((adc.cr2 & 0x40000000u), 0x40000000u); /* SWSTART was written */
    assert_int_equal((adc.sr & 0x02u), 0u);                 /* and EOC is clear again */

    /* A part that never sets the flag is reported, not waited on forever. */
    assert_false(soc_stm32f4_adc_read_regular(&adc, &value));

    /* Refusals, and nothing written by any of them. */
    assert_int_equal(soc_stm32f4_adc_init_regular(NULL, 11u, 1u), 0u);
    assert_int_equal(soc_stm32f4_adc_init_regular(&adc, 18u, 1u), 0u);
    assert_int_equal(soc_stm32f4_adc_init_regular(&adc, 11u, 8u), 0u);
    assert_false(soc_stm32f4_adc_read_regular(NULL, &value));
    assert_false(soc_stm32f4_adc_read_regular(&adc, NULL));
    /* A block that was never enabled does not start a conversion at all. */
    soc_stm32f4_adc_regs_t off;
    memset(&off, 0, sizeof(off));
    assert_false(soc_stm32f4_adc_read_regular(&off, &value));
    assert_int_equal(off.cr2, 0u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_soc_stm32f4_adc_injected_setup),
        cmocka_unit_test(test_soc_stm32f4_adc_reads_the_ranks),
        cmocka_unit_test(test_soc_stm32f4_adc_isr_forwards),
        cmocka_unit_test(test_soc_stm32f4_adc_regular_conversion),
        cmocka_unit_test(test_soc_stm32f4_adc_refuses_impossible_configs),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
