#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "contract/app_contract.h"
#include "dlt645/dlt645.h"
#include "edge/event.h"
#include "edge/modules.h"

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
    assert_ptr_equal(dlt645_module(&app), &app.module);
    assert_non_null(app.module.poll);
    assert_non_null(app.module.on_event);
    assert_non_null(app.module.power_off);
    assert_null(app.module.suspend);
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
    assert_int_equal(dlt645_init(&app), EDGE_EINVAL);

    dlt645_storage_if_t no_reader = {0};
    dlt645_construct(&app, 1u, 1u, &no_reader);
    assert_int_equal(dlt645_init(&app), EDGE_EINVAL);

    dlt645_storage_if_t good = {.read = fake_read};
    dlt645_construct(&app, 1u, 1u, &good);
    assert_int_equal(dlt645_init(&app), EDGE_OK);
}

static void test_poll_event_and_deinit(void **state) {
    (void)state;
    dlt645_storage_if_t storage = {.read = fake_read, .write = fake_write, .self = NULL};
    dlt645_t app;
    edge_event_t event = {.id = 0x1234u};

    dlt645_construct(&app, 1u, 1u, &storage);
    assert_int_equal(dlt645_init(&app), EDGE_OK);
    assert_int_equal(app.poll_count, 0u);
    assert_int_equal(app.module.poll(&app.module), EDGE_OK);
    assert_int_equal(app.module.poll(&app.module), EDGE_OK);
    assert_int_equal(app.poll_count, 2u);

    assert_int_equal(app.module.on_event(&app.module, &event), EDGE_OK);
    assert_int_equal(app.last_event, 0x1234u);
    assert_int_equal(app.module.on_event(&app.module, NULL), EDGE_EINVAL);

    assert_int_equal(app.module.power_off(&app.module), EDGE_OK);
    assert_int_equal(dlt645_deinit(&app), EDGE_OK);
    assert_null(app.storage);
}

static void test_null_private_data_is_rejected(void **state) {
    (void)state;
    dlt645_storage_if_t storage = {.read = fake_read};
    dlt645_t app;

    dlt645_construct(&app, 1u, 1u, &storage);
    app.module.private_data = NULL;
    assert_int_equal(app.module.poll(&app.module), EDGE_EINVAL);
    /* D51: init/deinit take the app pointer directly, so the analogue of a null
     * private_data is a null self. */
    assert_int_equal(dlt645_init(NULL), EDGE_EINVAL);
    assert_int_equal(dlt645_deinit(NULL), EDGE_EINVAL);
}

/* --- App contract (D51), adopted in a few lines ---
 * The suite is reusable: it checks identity, the callbacks sys relies on, that a
 * foreign event is ignored, and that init/release report success. */
static dlt645_t g_contract_app;
static dlt645_storage_if_t g_contract_storage = {
    .read = fake_read,
    .write = fake_write,
    .self = NULL,
};

static edge_status_t contract_prepare(void) {
    dlt645_construct(&g_contract_app, EDGE_MOD_DLT645, 100u, &g_contract_storage);
    return dlt645_init(&g_contract_app);
}

static edge_status_t contract_release(void) {
    return dlt645_deinit(&g_contract_app);
}

static const edge_module_t *contract_module(void) {
    return dlt645_module(&g_contract_app);
}

static void test_app_contract(void **state) {
    (void)state;
    const edge_app_contract_t contract = {
        .name = "dlt645",
        .prepare = contract_prepare,
        .release = contract_release,
        .module = contract_module,
        .module_id = EDGE_MOD_DLT645,
        .priority = 100u,
    };
    edge_contract_app_run(&contract);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_construct_binds_contract),
        cmocka_unit_test(test_construct_null_is_noop),
        cmocka_unit_test(test_init_requires_reader),
        cmocka_unit_test(test_poll_event_and_deinit),
        cmocka_unit_test(test_null_private_data_is_rejected),
        cmocka_unit_test(test_app_contract),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
