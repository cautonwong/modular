#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "ble_music/ble_music.h"
#include "edge/events.h"
#include "edge/modules.h"

static uint64_t s_mock_tick_ms = 100000ULL;
static uint8_t s_last_command_sent = 0xFF;

static uint64_t mock_get_tick_ms(void *self) {
    (void)self;
    return s_mock_tick_ms;
}

static edge_status_t mock_send_event(void *self, uint8_t event_byte) {
    (void)self;
    s_last_command_sent = event_byte;
    return EDGE_OK;
}

static void test_ble_music_state_and_progress(void **state) {
    (void)state;

    s_mock_tick_ms = 100000ULL;
    ble_music_transport_port_t transport = {
        .self = NULL,
        .get_tick_ms = mock_get_tick_ms,
        .send_event = mock_send_event,
    };
    edge_event_t event_storage[8];
    edge_event_queue_t queue;
    assert_int_equal(edge_event_queue_init(&queue, event_storage, 8u), EDGE_OK);

    edge_event_sink_t sink = {
        .queue = &queue,
        .clock = NULL,
        .guard = NULL,
    };

    ble_music_t music;
    ble_music_init(&music, &transport, &sink);

    ble_music_info_t info;
    assert_true(ble_music_get_info(&music, &info));
    assert_string_equal(info.artist, "Not Playing");
    assert_false(info.playing);

    /* Update track info */
    ble_music_set_artist(&music, "Daft Punk", 9);
    ble_music_set_track(&music, "Get Lucky", 9);
    ble_music_set_album(&music, "RAM", 3);
    ble_music_set_total_length(&music, 248);
    ble_music_set_position(&music, 30); /* 30 seconds into song */
    ble_music_set_playback_speed(&music, 100);
    ble_music_set_status(&music, true); /* Playing */

    assert_true(ble_music_get_info(&music, &info));
    assert_string_equal(info.artist, "Daft Punk");
    assert_string_equal(info.track, "Get Lucky");
    assert_string_equal(info.album, "RAM");
    assert_true(info.playing);
    assert_int_equal(info.track_length, 248);

    /* Advance 10 seconds */
    s_mock_tick_ms += 10000ULL;
    assert_int_equal(ble_music_get_progress(&music), 40);

    /* Advance another 15 seconds */
    s_mock_tick_ms += 15000ULL;
    assert_int_equal(ble_music_get_progress(&music), 55);

    /* Pause */
    ble_music_set_status(&music, false);
    assert_int_equal(ble_music_get_progress(&music), 55);

    /* Time passes while paused -> progress should stay at 55 */
    s_mock_tick_ms += 20000ULL;
    assert_int_equal(ble_music_get_progress(&music), 55);

    /* Commands */
    assert_int_equal(ble_music_send_command(&music, BLE_MUSIC_CMD_PLAY), EDGE_OK);
    assert_int_equal(s_last_command_sent, BLE_MUSIC_CMD_PLAY);

    assert_int_equal(ble_music_send_command(&music, BLE_MUSIC_CMD_NEXT), EDGE_OK);
    assert_int_equal(s_last_command_sent, BLE_MUSIC_CMD_NEXT);
}

static void test_ble_music_speeds_and_capping(void **state) {
    (void)state;

    s_mock_tick_ms = 100000ULL;
    ble_music_transport_port_t transport = {
        .self = NULL,
        .get_tick_ms = mock_get_tick_ms,
        .send_event = mock_send_event,
    };
    ble_music_t music;
    ble_music_init(&music, &transport, NULL);

    ble_music_set_total_length(&music, 100);
    ble_music_set_position(&music, 10);
    ble_music_set_playback_speed(&music, 200); // 2.0x speed
    ble_music_set_repeat(&music, true);
    ble_music_set_shuffle(&music, true);
    ble_music_set_track_number(&music, 5);
    ble_music_set_tracks_total(&music, 12);
    ble_music_set_status(&music, true);

    ble_music_info_t info;
    assert_true(ble_music_get_info(&music, &info));
    assert_true(info.repeat);
    assert_true(info.shuffle);
    assert_int_equal(info.track_number, 5);
    assert_int_equal(info.tracks_total, 12);

    // Advance 10 real seconds at 2.0x speed -> 20 track seconds elapsed -> progress = 30
    s_mock_tick_ms += 10000ULL;
    assert_int_equal(ble_music_get_progress(&music), 30);

    // Advance 50 real seconds (100 track seconds) -> progress should cap at track_length 100
    s_mock_tick_ms += 50000ULL;
    assert_int_equal(ble_music_get_progress(&music), 100);

    // Volume commands
    assert_int_equal(ble_music_send_command(&music, BLE_MUSIC_CMD_VOLUP), EDGE_OK);
    assert_int_equal(s_last_command_sent, BLE_MUSIC_CMD_VOLUP);
    assert_int_equal(ble_music_send_command(&music, BLE_MUSIC_CMD_VOLDOWN), EDGE_OK);
    assert_int_equal(s_last_command_sent, BLE_MUSIC_CMD_VOLDOWN);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ble_music_state_and_progress),
        cmocka_unit_test(test_ble_music_speeds_and_capping),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
