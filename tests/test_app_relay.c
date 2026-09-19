#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/event.h"
#include "edge/events.h"
#include "relay/relay.h"

typedef struct fake_out {
    int calls;
    uint8_t last_channel;
    bool last_on;
    edge_status_t rc;
} fake_out_t;

static edge_status_t fake_set(void *self, uint8_t channel, bool on) {
    fake_out_t *out = (fake_out_t *)self;
    ++out->calls;
    out->last_channel = channel;
    out->last_on = on;
    return out->rc;
}

static void test_construct_binds_contract(void **state) {
    (void)state;
    fake_out_t out = {0};
    relay_out_if_t iface = {.set = fake_set, .self = &out};
    relay_t relay;

    relay_construct(&relay, 0x1002u, 110u, &iface);
    assert_int_equal(relay.module.module_id, 0x1002u);
    assert_int_equal(relay.module.priority, 110u);
    assert_ptr_equal(relay.module.private_data, &relay);
    assert_ptr_equal(relay_module(&relay), &relay.module);
    assert_non_null(relay.module.on_event);
    assert_ptr_equal(relay.out, &iface);
}

static void test_construct_null_is_noop(void **state) {
    (void)state;
    relay_construct(NULL, 1u, 1u, NULL);
}

static void test_init_requires_output(void **state) {
    (void)state;
    relay_t relay;

    relay_construct(&relay, 1u, 1u, NULL);
    assert_int_equal(relay_init(&relay), EDGE_EINVAL);

    relay_out_if_t no_set = {0};
    relay_construct(&relay, 1u, 1u, &no_set);
    assert_int_equal(relay_init(&relay), EDGE_EINVAL);

    fake_out_t out = {0};
    relay_out_if_t good = {.set = fake_set, .self = &out};
    relay_construct(&relay, 1u, 1u, &good);
    assert_int_equal(relay_init(&relay), EDGE_OK);
    assert_int_equal(relay.module.poll(&relay.module), EDGE_OK);
}

static void test_event_drives_output(void **state) {
    (void)state;
    fake_out_t out = {0};
    relay_out_if_t iface = {.set = fake_set, .self = &out};
    relay_t relay;
    edge_event_t on = {.id = EDGE_EVT_RELAY_CHANGED, .arg0 = 2u, .arg1 = 1u};
    edge_event_t off = {.id = EDGE_EVT_RELAY_CHANGED, .arg0 = 2u, .arg1 = 0u};
    edge_event_t other = {.id = EDGE_EVT_UART0_RX};

    relay_construct(&relay, 1u, 1u, &iface);
    assert_int_equal(relay_init(&relay), EDGE_OK);

    assert_int_equal(relay.module.on_event(&relay.module, &on), EDGE_OK);
    assert_int_equal(out.calls, 1);
    assert_int_equal(out.last_channel, 2u);
    assert_true(out.last_on);
    assert_int_equal(relay.state, 1u);
    assert_int_equal(relay.toggles, 1u);

    assert_int_equal(relay.module.on_event(&relay.module, &off), EDGE_OK);
    assert_false(out.last_on);
    assert_int_equal(relay.state, 0u);
    assert_int_equal(relay.toggles, 2u);

    assert_int_equal(relay.module.on_event(&relay.module, &other), EDGE_OK);
    assert_int_equal(out.calls, 2);
    assert_int_equal(relay.last_event, EDGE_EVT_UART0_RX);

    assert_int_equal(relay.module.on_event(&relay.module, NULL), EDGE_EINVAL);
}

static void test_event_error_propagates(void **state) {
    (void)state;
    fake_out_t out = {.rc = EDGE_EIO};
    relay_out_if_t iface = {.set = fake_set, .self = &out};
    relay_t relay;
    edge_event_t event = {.id = EDGE_EVT_RELAY_CHANGED, .arg0 = 0u, .arg1 = 1u};

    relay_construct(&relay, 1u, 1u, &iface);
    assert_int_equal(relay_init(&relay), EDGE_OK);
    assert_int_equal(relay.module.on_event(&relay.module, &event), EDGE_EIO);
    assert_int_equal(relay.toggles, 0u);
}

static void test_power_off_and_deinit(void **state) {
    (void)state;
    fake_out_t out = {0};
    relay_out_if_t iface = {.set = fake_set, .self = &out};
    relay_t relay;

    relay_construct(&relay, 1u, 1u, &iface);
    assert_int_equal(relay_init(&relay), EDGE_OK);
    assert_int_equal(relay.module.power_off(&relay.module), EDGE_OK);
    assert_int_equal(out.calls, 1);
    assert_false(out.last_on);
    assert_int_equal(relay_deinit(&relay), EDGE_OK);
    assert_null(relay.out);
}

static void test_null_private_data_is_rejected(void **state) {
    (void)state;
    fake_out_t out = {0};
    relay_out_if_t iface = {.set = fake_set, .self = &out};
    relay_t relay;

    relay_construct(&relay, 1u, 1u, &iface);
    relay.module.private_data = NULL;
    assert_int_equal(relay.module.poll(&relay.module), EDGE_EINVAL);
    assert_int_equal(relay.module.on_event(&relay.module, &(edge_event_t){0}), EDGE_EINVAL);
    assert_int_equal(relay.module.power_off(&relay.module), EDGE_EINVAL);
    /* D51: init/deinit take the app pointer directly. */
    assert_int_equal(relay_init(NULL), EDGE_EINVAL);
    assert_int_equal(relay_deinit(NULL), EDGE_EINVAL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_construct_binds_contract),
        cmocka_unit_test(test_construct_null_is_noop),
        cmocka_unit_test(test_init_requires_output),
        cmocka_unit_test(test_event_drives_output),
        cmocka_unit_test(test_event_error_propagates),
        cmocka_unit_test(test_power_off_and_deinit),
        cmocka_unit_test(test_null_private_data_is_rejected),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
