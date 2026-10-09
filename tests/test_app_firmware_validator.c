#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "edge/events.h"
#include "edge/modules.h"
#include "firmware_validator/firmware_validator.h"

static uint32_t s_mock_internal_flash_word = 0xFFFFFFFFu;
static bool s_mock_reset_called = false;

static edge_status_t mock_read_word(void *self, uint32_t address, uint32_t *value) {
    (void)self;
    if (address == FIRMWARE_VALIDATOR_VALID_BIT_ADDR) {
        *value = s_mock_internal_flash_word;
        return EDGE_OK;
    }
    return EDGE_EINVAL;
}

static edge_status_t mock_write_word(void *self, uint32_t address, uint32_t value) {
    (void)self;
    if (address == FIRMWARE_VALIDATOR_VALID_BIT_ADDR) {
        s_mock_internal_flash_word = value;
        return EDGE_OK;
    }
    return EDGE_EINVAL;
}

static edge_status_t mock_reset(void *self) {
    (void)self;
    s_mock_reset_called = true;
    return EDGE_OK;
}

static void test_firmware_validator_flow(void **state) {
    (void)state;

    firmware_validator_port_t port = {
        .self = NULL,
        .read_word = mock_read_word,
        .write_word = mock_write_word,
        .system_reset = mock_reset,
    };

    firmware_validator_t val;
    firmware_validator_init(&val, &port, NULL);

    s_mock_internal_flash_word = 0xFFFFFFFFu;
    assert_false(firmware_validator_is_validated(&val));

    assert_int_equal(firmware_validator_validate(&val), EDGE_OK);
    assert_int_equal(s_mock_internal_flash_word, FIRMWARE_VALIDATOR_VALID_BIT_VAL);
    assert_true(firmware_validator_is_validated(&val));

    s_mock_reset_called = false;
    assert_int_equal(firmware_validator_reset(&val), EDGE_OK);
    assert_true(s_mock_reset_called);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_firmware_validator_flow),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
