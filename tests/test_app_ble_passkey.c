#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "ble_passkey/ble_passkey.h"
#include "edge/modules.h"

static void test_ble_passkey_lifecycle(void **state) {
    (void)state;
    ble_passkey_app_t passkey;
    assert_int_equal(ble_passkey_init(&passkey), EDGE_OK);
    assert_int_equal(passkey.module.module_id, EDGE_MOD_BLE_PASSKEY);
    assert_int_equal(ble_passkey_get_state(&passkey), BLE_PASSKEY_IDLE);

    /* Show 6-digit key 123456 */
    assert_int_equal(ble_passkey_show(&passkey, 123456u, 30000u), EDGE_OK);
    assert_int_equal(ble_passkey_get_key(&passkey), 123456u);
    assert_int_equal(ble_passkey_get_state(&passkey), BLE_PASSKEY_DISPLAYING);

    /* Confirm */
    ble_passkey_confirm(&passkey);
    assert_int_equal(ble_passkey_get_state(&passkey), BLE_PASSKEY_CONFIRMED);

    /* Dismiss */
    ble_passkey_dismiss(&passkey);
    assert_int_equal(ble_passkey_get_state(&passkey), BLE_PASSKEY_IDLE);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ble_passkey_lifecycle),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
