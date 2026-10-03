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
#include "zmk_studio/studio.h"

typedef struct mock_transport {
    int send_calls;
    uint8_t last_frame[256];
    size_t last_len;
} mock_transport_t;

static edge_status_t mock_send_frame(void *self, const uint8_t *frame_data, size_t len) {
    mock_transport_t *t = (mock_transport_t *)self;
    t->send_calls++;
    t->last_len = len;
    if (len <= sizeof(t->last_frame)) {
        memcpy(t->last_frame, frame_data, len);
    }
    return EDGE_OK;
}

typedef struct mock_keymap {
    uint16_t bindings[8][64];
    int save_calls;
    int discard_calls;
} mock_keymap_t;

static edge_status_t mock_get_layer_binding(void *self, uint8_t layer, uint32_t pos,
                                            uint16_t *out_bhv, uint32_t *out_p1, uint32_t *out_p2) {
    mock_keymap_t *k = (mock_keymap_t *)self;
    if (layer >= 8 || pos >= 64) {
        return EDGE_EINVAL;
    }
    *out_bhv = k->bindings[layer][pos];
    *out_p1 = 0x04;
    *out_p2 = 0;
    return EDGE_OK;
}

static edge_status_t mock_set_layer_binding(void *self, uint8_t layer, uint32_t pos, uint16_t bhv,
                                            uint32_t p1, uint32_t p2) {
    (void)p1;
    (void)p2;
    mock_keymap_t *k = (mock_keymap_t *)self;
    if (layer >= 8 || pos >= 64) {
        return EDGE_EINVAL;
    }
    k->bindings[layer][pos] = bhv;
    return EDGE_OK;
}

static edge_status_t mock_save_changes(void *self) {
    mock_keymap_t *k = (mock_keymap_t *)self;
    k->save_calls++;
    return EDGE_OK;
}

static edge_status_t mock_discard_changes(void *self) {
    mock_keymap_t *k = (mock_keymap_t *)self;
    k->discard_calls++;
    return EDGE_OK;
}

static void test_studio_framing_encode_decode(void **state) {
    (void)state;
    uint8_t payload[] = {0x01, 0xAB, 0x02, 0xAC, 0x03, 0xAD, 0x04};
    uint8_t encoded[32];
    size_t enc_len = zmk_studio_frame_encode(payload, sizeof(payload), encoded, sizeof(encoded));
    assert_true(enc_len > sizeof(payload));

    // Verify SOF and EOF
    assert_int_equal(encoded[0], ZMK_STUDIO_FRAMING_SOF);
    assert_int_equal(encoded[enc_len - 1], ZMK_STUDIO_FRAMING_EOF);

    // Decode byte by byte
    zmk_studio_framing_state_t rx_state = ZMK_STUDIO_STATE_IDLE;
    uint8_t decoded[32];
    size_t dec_len = 0;

    for (size_t i = 0; i < enc_len; i++) {
        uint8_t out_b = 0;
        if (zmk_studio_framing_process_byte(&rx_state, encoded[i], &out_b)) {
            decoded[dec_len++] = out_b;
        }
    }

    assert_int_equal(dec_len, sizeof(payload));
    assert_memory_equal(decoded, payload, sizeof(payload));
}

static void test_studio_core_and_keymap_rpc(void **state) {
    (void)state;
    mock_transport_t transport_ctx = {0};
    zmk_studio_transport_if_t transport = {
        .self = &transport_ctx,
        .send_frame = mock_send_frame,
    };
    mock_keymap_t keymap_ctx = {0};
    keymap_ctx.bindings[0][5] = 0x42; // Behavior 0x42 on layer 0 pos 5
    zmk_studio_keymap_if_t keymap = {
        .self = &keymap_ctx,
        .get_layer_binding = mock_get_layer_binding,
        .set_layer_binding = mock_set_layer_binding,
        .save_changes = mock_save_changes,
        .discard_changes = mock_discard_changes,
    };

    zmk_studio_app_t app;
    zmk_studio_construct(&app, EDGE_MOD_ZMK_STUDIO, 80, &transport, &keymap);
    assert_int_equal(zmk_studio_init(&app), EDGE_OK);

    // 1. Get Lock State -> initially locked
    uint8_t req_lock[] = {ZMK_STUDIO_SUBSYS_CORE, ZMK_STUDIO_CORE_CMD_GET_LOCK_STATE};
    assert_int_equal(zmk_studio_process_request(&app, req_lock, sizeof(req_lock)), EDGE_OK);
    assert_int_equal(transport_ctx.send_calls, 1);
    assert_false(zmk_studio_is_unlocked(&app));

    // 2. Set keymap while locked -> rejected
    uint8_t req_set_binding[15] = {0};
    req_set_binding[0] = ZMK_STUDIO_SUBSYS_KEYMAP;
    req_set_binding[1] = ZMK_STUDIO_KEYMAP_CMD_SET_LAYER_BINDING;
    req_set_binding[2] = 0; // Layer 0
    req_set_binding[3] = 5; // Pos 5
    uint16_t new_bhv = 0x99;
    memcpy(&req_set_binding[4], &new_bhv, 2);
    assert_int_equal(zmk_studio_process_request(&app, req_set_binding, sizeof(req_set_binding)),
                     EDGE_OK);
    assert_int_not_equal(keymap_ctx.bindings[0][5], 0x99); // Still 0x42

    // 3. Unlock device
    uint8_t req_unlock[] = {ZMK_STUDIO_SUBSYS_CORE, ZMK_STUDIO_CORE_CMD_UNLOCK_DEVICE};
    assert_int_equal(zmk_studio_process_request(&app, req_unlock, sizeof(req_unlock)), EDGE_OK);
    assert_true(zmk_studio_is_unlocked(&app));

    // 4. Set keymap while unlocked -> succeeds
    assert_int_equal(zmk_studio_process_request(&app, req_set_binding, sizeof(req_set_binding)),
                     EDGE_OK);
    assert_int_equal(keymap_ctx.bindings[0][5], 0x99);

    // 5. Save changes
    uint8_t req_save[] = {ZMK_STUDIO_SUBSYS_KEYMAP, ZMK_STUDIO_KEYMAP_CMD_SAVE_CHANGES};
    assert_int_equal(zmk_studio_process_request(&app, req_save, sizeof(req_save)), EDGE_OK);
    assert_int_equal(keymap_ctx.save_calls, 1);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_studio_framing_encode_decode),
        cmocka_unit_test(test_studio_core_and_keymap_rpc),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
