/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_endpoints/endpoints.h"

typedef struct mock_endpoint_sink {
    int calls;
    uint8_t last_endpoint;
    uint8_t last_profile;
} mock_endpoint_sink_t;

static edge_status_t mock_post_endpoint(void *self, uint8_t endpoint, uint8_t active_profile) {
    mock_endpoint_sink_t *sink = (mock_endpoint_sink_t *)self;
    sink->calls++;
    sink->last_endpoint = endpoint;
    sink->last_profile = active_profile;
    return EDGE_OK;
}

static void test_endpoints_selection_and_profiles(void **state) {
    (void)state;
    mock_endpoint_sink_t sink_ctx = {0};
    zmk_endpoint_event_sink_if_t sink_if = {.self = &sink_ctx,
                                            .post_endpoint_changed = mock_post_endpoint};

    zmk_endpoints_app_t app;
    zmk_endpoints_construct(&app, EDGE_MOD_ZMK_ENDPOINTS, 50, &sink_if);
    assert_int_equal(zmk_endpoints_init(&app), EDGE_OK);

    assert_int_equal(zmk_endpoints_get_current_endpoint(&app), ZMK_ENDPOINT_USB);
    assert_int_equal(zmk_endpoints_get_active_profile(&app), 0);

    // Switch to BLE
    assert_int_equal(zmk_endpoints_select_endpoint(&app, ZMK_ENDPOINT_BLE), EDGE_OK);
    assert_int_equal(zmk_endpoints_get_current_endpoint(&app), ZMK_ENDPOINT_BLE);
    assert_int_equal(sink_ctx.calls, 1);
    assert_int_equal(sink_ctx.last_endpoint, ZMK_ENDPOINT_BLE);

    // Toggle back to USB
    assert_int_equal(zmk_endpoints_toggle_endpoint(&app), EDGE_OK);
    assert_int_equal(zmk_endpoints_get_current_endpoint(&app), ZMK_ENDPOINT_USB);

    // Next profile -> 1
    assert_int_equal(zmk_endpoints_next_profile(&app), EDGE_OK);
    assert_int_equal(zmk_endpoints_get_active_profile(&app), 1);

    // Next profile -> 2, 3, 4, 0 (wrap around)
    zmk_endpoints_next_profile(&app); // 2
    zmk_endpoints_next_profile(&app); // 3
    zmk_endpoints_next_profile(&app); // 4
    zmk_endpoints_next_profile(&app); // 0
    assert_int_equal(zmk_endpoints_get_active_profile(&app), 0);

    // Prev profile -> 4 (wrap around backwards)
    zmk_endpoints_prev_profile(&app);
    assert_int_equal(zmk_endpoints_get_active_profile(&app), 4);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_endpoints_selection_and_profiles),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
