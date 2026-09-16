#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "dlt645/dlt645.h"
#include "edge/event.h"

static int g_read_calls;
static int g_write_calls;

static edge_status_t fake_read(void *self, uint32_t key, void *buf, size_t len) {
    (void)self;
    (void)key;
    (void)buf;
    (void)len;
    ++g_read_calls;
    return EDGE_OK;
}

static edge_status_t fake_write(void *self, uint32_t key, const void *buf, size_t len) {
    (void)self;
    (void)key;
    (void)buf;
    (void)len;
    ++g_write_calls;
    return EDGE_OK;
}

static void test_construct_binds_contract(void **state) {
    (void)state;
    dlt645_storage_if_t storage = {.read = fake_read, .write = fake_write, .self = NULL};
    dlt645_t app;

    dlt645_construct(&app, 0x1001u, 10u, &storage);
    assert_int_equal(app.module.module_id, 0x1001u);
    assert_int_equal(app.module.priority, 10u);
    assert_ptr_equal(app.module.private_data, &app);
    assert_non_null(app.module.init);
    assert_non_null(app.module.poll);
    assert_non_null(app.module.on_event);
    assert_non_null(app.module.power_off);
    assert_non_null(app.module.deinit);
    assert_ptr_equal(app.storage, &storage);
}

static void test_construct_null_is_noop(void **state) {
    (void)state;
    dlt645_construct(NULL, 1u, 1u, NULL);
}

static void test_init_requires_reader(void **state) {
    (void)state;
    dlt645_t app;

    dlt645_construct(&app, 1u, 1u, NULL);
    assert_int_equal(app.module.init(&app.module), EDGE_EINVAL);

    dlt645_storage_if_t no_reader = {0};
    dlt645_construct(&app, 1u, 1u, &no_reader);
    assert_int_equal(app.module.init(&app.module), EDGE_EINVAL);

    dlt645_storage_if_t good = {.read = fake_read};
    dlt645_construct(&app, 1u, 1u, &good);
    assert_int_equal(app.module.init(&app.module), EDGE_OK);
}

static void test_poll_event_and_deinit(void **state) {
    (void)state;
    dlt645_storage_if_t storage = {.read = fake_read, .write = fake_write, .self = NULL};
    dlt645_t app;
    edge_event_t event = {.id = 0x1234u};

    dlt645_construct(&app, 1u, 1u, &storage);
    assert_int_equal(app.module.init(&app.module), EDGE_OK);
    assert_int_equal(app.poll_count, 0u);
    assert_int_equal(app.module.poll(&app.module), EDGE_OK);
    assert_int_equal(app.module.poll(&app.module), EDGE_OK);
    assert_int_equal(app.poll_count, 2u);

    assert_int_equal(app.module.on_event(&app.module, &event), EDGE_OK);
    assert_int_equal(app.last_event, 0x1234u);
    assert_int_equal(app.module.on_event(&app.module, NULL), EDGE_EINVAL);

    assert_int_equal(app.module.power_off(&app.module), EDGE_OK);
    assert_int_equal(app.module.deinit(&app.module), EDGE_OK);
    assert_null(app.storage);
}

static void test_null_private_data_is_rejected(void **state) {
    (void)state;
    dlt645_storage_if_t storage = {.read = fake_read};
    dlt645_t app;

    dlt645_construct(&app, 1u, 1u, &storage);
    app.module.private_data = NULL;
    assert_int_equal(app.module.init(&app.module), EDGE_EINVAL);
    assert_int_equal(app.module.poll(&app.module), EDGE_EINVAL);
    assert_int_equal(app.module.deinit(&app.module), EDGE_EINVAL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_construct_binds_contract),
        cmocka_unit_test(test_construct_null_is_noop),
        cmocka_unit_test(test_init_requires_reader),
        cmocka_unit_test(test_poll_event_and_deinit),
        cmocka_unit_test(test_null_private_data_is_rejected),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
