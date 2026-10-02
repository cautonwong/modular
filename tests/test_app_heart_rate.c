#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <cmocka.h>

#include "edge/events.h"
#include "edge/modules.h"
#include "heart_rate/heart_rate.h"

typedef struct mock_sensor {
    float phase;
    uint16_t als;
    bool enabled;
} mock_sensor_t;

static edge_status_t mock_read_sample(void *self, uint16_t *out_hrs, uint16_t *out_als) {
    mock_sensor_t *mock = (mock_sensor_t *)self;
    if (mock == NULL || out_hrs == NULL || out_als == NULL) {
        return EDGE_EINVAL;
    }
    // Generate synthetic 1.2 Hz (72 BPM) sine wave on DC offset 10000
    // Real-world PPG AC variation is ~50-150 counts on ~10000 DC baseline
    // Sampling rate = 10Hz (dt = 0.1s)
    float val = 10000.0f + 100.0f * sinf(mock->phase);
    mock->phase += 2.0f * 3.14159265f * 1.2f * 0.1f;
    *out_hrs = (uint16_t)val;
    *out_als = mock->als;
    return EDGE_OK;
}

static edge_status_t mock_enable(void *self, bool enable) {
    mock_sensor_t *mock = (mock_sensor_t *)self;
    if (mock == NULL) {
        return EDGE_EINVAL;
    }
    mock->enabled = enable;
    return EDGE_OK;
}

static void test_heart_rate_sine_detection(void **state) {
    (void)state;
    mock_sensor_t mock = {.phase = 0.0f, .als = 100, .enabled = false};
    ppg_sensor_if_t sensor_if = {
        .read_sample = mock_read_sample,
        .enable = mock_enable,
        .self = &mock,
    };

    heart_rate_t hr;
    heart_rate_construct(&hr, EDGE_MOD_HEART_RATE, 100u, &sensor_if);
    assert_int_equal(heart_rate_init(&hr), EDGE_OK);
    assert_true(mock.enabled);

    // Feed 100 samples (10 seconds)
    int bpm = 0;
    for (int i = 0; i < 100; i++) {
        uint16_t raw_hrs = 0;
        uint16_t raw_als = 0;
        mock_read_sample(&mock, &raw_hrs, &raw_als);
        heart_rate_process_sample(&hr, raw_hrs, raw_als, &bpm);
    }

    uint8_t final_bpm = heart_rate_get_bpm(&hr);
    assert_in_range(final_bpm, 68, 76);

    assert_int_equal(heart_rate_shutdown(&hr), EDGE_OK);
    assert_false(mock.enabled);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_heart_rate_sine_detection),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
