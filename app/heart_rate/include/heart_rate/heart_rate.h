#ifndef APP_HEART_RATE_H
#define APP_HEART_RATE_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "heart_rate/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PPG_DATA_LENGTH 64u
#define PPG_SPECTRUM_LENGTH (PPG_DATA_LENGTH >> 1u)
#define PPG_OVERLAP_WINDOW 5u
#define PPG_SPECTRAL_AVG_MAX 2u
#define PPG_AVG_HISTORY_LEN 4u

typedef struct heart_rate {
    edge_module_t module;
    const ppg_sensor_if_t *sensor;

    /* PPG Acquisition Buffer */
    uint16_t data_hrs[PPG_DATA_LENGTH];
    uint16_t data_index;
    bool enough_data;
    bool reset_spectral_avg;
    uint16_t spectral_avg_count;

    /* Working FFT Buffers */
    float v_real[PPG_DATA_LENGTH];
    float v_imag[PPG_DATA_LENGTH];
    float spectrum[PPG_SPECTRUM_LENGTH];

    /* Statistics & Heart Rate State */
    float data_average[PPG_AVG_HISTORY_LEN];
    uint8_t avg_index;
    float last_peak_location;
    uint16_t als_threshold;
    uint16_t als_value;

    uint8_t current_bpm;
    bool is_running;
    uint32_t poll_count;
} heart_rate_t;

void heart_rate_construct(heart_rate_t *self, uint32_t module_id, uint32_t priority,
                          const ppg_sensor_if_t *sensor);

edge_status_t heart_rate_init(heart_rate_t *self);
edge_status_t heart_rate_shutdown(heart_rate_t *self);

/**
 * Feed one sample into the PPG pipeline and compute heart rate if a full window is ready.
 *
 * @param self Pointer to heart_rate instance.
 * @param hrs Raw 16-bit HRS ADC reading.
 * @param als Raw 16-bit ALS reading.
 * @param out_bpm Output pointer to receive BPM (or -1 if reset, 0 if computing/no peak).
 * @return EDGE_OK on success.
 */
edge_status_t heart_rate_process_sample(heart_rate_t *self, uint16_t hrs, uint16_t als,
                                        int *out_bpm);

uint8_t heart_rate_get_bpm(const heart_rate_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_HEART_RATE_H */
