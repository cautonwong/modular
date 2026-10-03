/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_settings/settings.h"

typedef struct mock_flash {
    uint8_t buffer[512];
    int read_calls;
    int write_calls;
    int erase_calls;
} mock_flash_t;

static edge_status_t mock_flash_read(void *self, uint32_t offset, uint8_t *buf, size_t len) {
    mock_flash_t *f = (mock_flash_t *)self;
    f->read_calls++;
    if (offset + len > sizeof(f->buffer)) {
        return EDGE_ENOSPC;
    }
    memcpy(buf, &f->buffer[offset], len);
    return EDGE_OK;
}

static edge_status_t mock_flash_write(void *self, uint32_t offset, const uint8_t *buf, size_t len) {
    mock_flash_t *f = (mock_flash_t *)self;
    f->write_calls++;
    if (offset + len > sizeof(f->buffer)) {
        return EDGE_ENOSPC;
    }
    memcpy(&f->buffer[offset], buf, len);
    return EDGE_OK;
}

static edge_status_t mock_flash_erase(void *self, uint32_t offset, size_t len) {
    mock_flash_t *f = (mock_flash_t *)self;
    f->erase_calls++;
    if (offset + len > sizeof(f->buffer)) {
        return EDGE_ENOSPC;
    }
    memset(&f->buffer[offset], 0xFF, len);
    return EDGE_OK;
}

static void test_settings_save_and_load_roundtrip(void **state) {
    (void)state;
    mock_flash_t flash_ctx = {0};
    zmk_settings_storage_if_t storage = {
        .self = &flash_ctx,
        .read = mock_flash_read,
        .write = mock_flash_write,
        .erase = mock_flash_erase,
    };

    zmk_settings_app_t app;
    zmk_settings_construct(&app, EDGE_MOD_ZMK_SETTINGS, 60, &storage);
    assert_int_equal(zmk_settings_init(&app), EDGE_OK);

    // Initial load on empty flash falls back to defaults and marks dirty
    assert_true(zmk_settings_is_dirty(&app));
    assert_int_equal(zmk_settings_save(&app), EDGE_OK);
    assert_false(zmk_settings_is_dirty(&app));

    // Modify a setting
    zmk_settings_record_t *rec = zmk_settings_get_record(&app);
    assert_non_null(rec);
    rec->selected_ble_profile = 3;
    rec->rgb_hue = 180;
    rec->rgb_val = 200;
    assert_true(zmk_settings_is_dirty(&app));

    // Save via poll
    assert_int_equal(app.module.poll(&app.module), EDGE_OK);
    assert_false(zmk_settings_is_dirty(&app));

    // Reload into a new instance
    zmk_settings_app_t app2;
    zmk_settings_construct(&app2, EDGE_MOD_ZMK_SETTINGS, 60, &storage);
    assert_int_equal(zmk_settings_init(&app2), EDGE_OK);

    zmk_settings_record_t *rec2 = zmk_settings_get_record(&app2);
    assert_int_equal(rec2->selected_ble_profile, 3);
    assert_int_equal(rec2->rgb_hue, 180);
    assert_int_equal(rec2->rgb_val, 200);
}

static void test_settings_reset(void **state) {
    (void)state;
    mock_flash_t flash_ctx = {0};
    zmk_settings_storage_if_t storage = {
        .self = &flash_ctx,
        .read = mock_flash_read,
        .write = mock_flash_write,
        .erase = mock_flash_erase,
    };

    zmk_settings_app_t app;
    zmk_settings_construct(&app, EDGE_MOD_ZMK_SETTINGS, 60, &storage);
    assert_int_equal(zmk_settings_init(&app), EDGE_OK);

    zmk_settings_record_t *rec = zmk_settings_get_record(&app);
    rec->selected_ble_profile = 4;
    zmk_settings_save(&app);

    // Reset to defaults
    assert_int_equal(zmk_settings_reset(&app), EDGE_OK);
    rec = zmk_settings_get_record(&app);
    assert_int_equal(rec->selected_ble_profile, 0);
    assert_int_equal(rec->magic, ZMK_SETTINGS_MAGIC);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_settings_save_and_load_roundtrip),
        cmocka_unit_test(test_settings_reset),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
