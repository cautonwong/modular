#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "ble_notifications/ble_notifications.h"
#include "edge/events.h"
#include "edge/modules.h"

static uint8_t s_mock_call_response = 0xFF;

static edge_status_t mock_send_call_response(void *self, uint8_t code) {
    (void)self;
    s_mock_call_response = code;
    return EDGE_OK;
}

static void test_ble_notifications_ring_buffer_and_ans(void **state) {
    (void)state;

    ble_notifications_call_port_t call_port = {
        .self = NULL,
        .send_call_response = mock_send_call_response,
    };
    edge_event_t event_storage[8];
    edge_event_queue_t queue;
    assert_int_equal(edge_event_queue_init(&queue, event_storage, 8u), EDGE_OK);

    edge_event_sink_t sink = {
        .queue = &queue,
        .clock = NULL,
        .guard = NULL,
    };

    ble_notifications_t notifs;
    ble_notifications_init(&notifs, &call_port, &sink);

    assert_int_equal(ble_notifications_count(&notifs), 0);

    /* Push 3 notifications directly */
    uint8_t id1 = ble_notifications_push(&notifs, BLE_NOTIF_CAT_SMS, "Hello Alice", 11);
    uint8_t id2 = ble_notifications_push(&notifs, BLE_NOTIF_CAT_EMAIL, "Meeting at 2pm", 14);
    uint8_t id3 = ble_notifications_push(&notifs, BLE_NOTIF_CAT_NEWS, "Breaking News", 13);

    assert_int_equal(ble_notifications_count(&notifs), 3);
    assert_int_equal(edge_event_count(&queue), 3);

    edge_event_t ev;
    assert_int_equal(edge_event_pop(&queue, &ev), EDGE_OK);
    assert_int_equal(ev.id, EDGE_EVT_WATCH_NOTIF_NEW);

    /* Index 0 is newest (id3) */
    ble_notification_item_t item;
    assert_true(ble_notifications_get_at_index(&notifs, 0, &item));
    assert_int_equal(item.id, id3);
    assert_string_equal(item.message, "Breaking News");
    assert_int_equal(item.category, BLE_NOTIF_CAT_NEWS);

    /* Query by ID */
    assert_true(ble_notifications_get_by_id(&notifs, id1, &item));
    assert_string_equal(item.message, "Hello Alice");

    /* ANS packet test: incoming call packet (3 header bytes + "John Doe") */
    uint8_t ans_pkt[11] = {0x03, 0x01, 0x00, 'J', 'o', 'h', 'n', ' ', 'D', 'o', 'e'};
    assert_int_equal(ble_notifications_process_ans_packet(&notifs, ans_pkt, sizeof(ans_pkt)),
                     EDGE_OK);

    assert_int_equal(ble_notifications_count(&notifs), 4);
    assert_true(ble_notifications_get_at_index(&notifs, 0, &item));
    assert_int_equal(item.category, BLE_NOTIF_CAT_INCOMING_CALL);
    assert_string_equal(item.message, "John Doe");

    /* Call responses */
    assert_int_equal(ble_notifications_accept_call(&notifs), EDGE_OK);
    assert_int_equal(s_mock_call_response, BLE_NOTIF_CALL_ACCEPT);

    assert_int_equal(ble_notifications_reject_call(&notifs), EDGE_OK);
    assert_int_equal(s_mock_call_response, BLE_NOTIF_CALL_REJECT);

    assert_int_equal(ble_notifications_mute_call(&notifs), EDGE_OK);
    assert_int_equal(s_mock_call_response, BLE_NOTIF_CALL_MUTE);

    /* Dismiss */
    assert_true(ble_notifications_dismiss(&notifs, id2));
    assert_int_equal(ble_notifications_count(&notifs), 3);
    assert_false(ble_notifications_get_by_id(&notifs, id2, &item));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ble_notifications_ring_buffer_and_ans),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
