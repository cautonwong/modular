#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/event.h"
#include "meter_core/meter_core.h"

static void test_construct_api_and_lifecycle(void **state) {
    (void)state;
    meter_core_t meter;
    uint16_t value = 0u;
    bool level = false;
    edge_event_t event = {.id = 0x42u};

    meter_core_construct(&meter, 0x1400u, 100u);
    assert_int_equal(meter.module.module_id, 0x1400u);
    assert_ptr_equal(meter.module.private_data, &meter);
    assert_int_equal(meter_core_init(&meter), EDGE_OK);

    assert_int_equal(meter_core_read_register(&meter, 0u, &value), EDGE_OK);
    assert_int_equal(value, 0u);
    assert_int_equal(meter_core_write_register(&meter, 2u, 0x1234u), EDGE_OK);
    assert_int_equal(meter_core_read_register(&meter, 2u, &value), EDGE_OK);
    assert_int_equal(value, 0x1234u);

    assert_int_equal(meter_core_read_register(NULL, 0u, &value), EDGE_EINVAL);
    assert_int_equal(meter_core_read_register(&meter, 0u, NULL), EDGE_EINVAL);
    assert_int_equal(meter_core_read_register(&meter, 99u, &value), EDGE_ENOENT);
    assert_int_equal(meter_core_write_register(&meter, 99u, 1u), EDGE_ENOENT);
    assert_int_equal(meter_core_write_register(NULL, 0u, 1u), EDGE_EINVAL);

    assert_int_equal(meter_core_write_coil(&meter, 1u, true), EDGE_OK);
    assert_int_equal(meter_core_read_coil(&meter, 1u, &level), EDGE_OK);
    assert_true(level);
    assert_int_equal(meter_core_read_coil(&meter, 99u, &level), EDGE_ENOENT);
    assert_int_equal(meter_core_read_coil(NULL, 0u, &level), EDGE_EINVAL);
    assert_int_equal(meter_core_read_coil(&meter, 0u, NULL), EDGE_EINVAL);
    assert_int_equal(meter_core_write_coil(&meter, 99u, true), EDGE_ENOENT);
    assert_int_equal(meter_core_write_coil(NULL, 0u, true), EDGE_EINVAL);

    meter_core_pulse(&meter, 5u);
    assert_int_equal(meter.pulses, 5u);
    assert_int_equal(meter.registers[0], 5u);
    meter_core_pulse(&meter, 0x10001u);
    assert_int_equal(meter.registers[1], 1u);
    meter_core_pulse(NULL, 1u);

    assert_int_equal(meter.module.poll(&meter.module), EDGE_OK);
    assert_int_equal(meter.polls, 1u);
    assert_int_equal(meter.module.on_event(&meter.module, &event), EDGE_OK);
    assert_int_equal(meter.last_event, 0x42u);
    assert_int_equal(meter.module.on_event(&meter.module, NULL), EDGE_EINVAL);
    assert_int_equal(meter.module.power_off(&meter.module), EDGE_OK);
    assert_int_equal(meter_core_deinit(&meter), EDGE_OK);

    meter_core_construct(NULL, 1u, 1u);
}

static void test_null_private_data_is_rejected(void **state) {
    (void)state;
    meter_core_t meter;
    edge_event_t event = {.id = 1u};

    meter_core_construct(&meter, 1u, 1u);
    meter.module.private_data = NULL;
    assert_int_equal(meter.module.poll(&meter.module), EDGE_EINVAL);
    assert_int_equal(meter.module.on_event(&meter.module, &event), EDGE_EINVAL);
    /* D51: init/deinit take the app pointer directly. */
    assert_int_equal(meter_core_init(NULL), EDGE_EINVAL);
    assert_int_equal(meter_core_deinit(NULL), EDGE_EINVAL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_construct_api_and_lifecycle),
        cmocka_unit_test(test_null_private_data_is_rejected),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
