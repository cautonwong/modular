#ifndef DIGITAL_FILTER_H
#define DIGITAL_FILTER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct biquad_filter {
    float b0, b1, b2;
    float a1, a2;
    float x1, x2;
    float y1, y2;
} biquad_filter_t;

typedef struct lowpass_filter {
    float cutoff_hz;
    float sample_rate_hz;
    float alpha;
    float prev_output;
    bool initialized;
} lowpass_filter_t;

typedef struct moving_average_filter {
    float buffer[32];
    uint32_t size;
    uint32_t head;
    float sum;
    uint32_t count;
} moving_average_filter_t;

/* Biquad Filter */
void biquad_init_lowpass(biquad_filter_t *filter, float sample_rate, float cutoff_freq, float q);
void biquad_init_notch(biquad_filter_t *filter, float sample_rate, float center_freq, float q);
float biquad_process(biquad_filter_t *filter, float input);
void biquad_reset(biquad_filter_t *filter);

/* Lowpass First-Order Filter */
void lowpass_init(lowpass_filter_t *filter, float cutoff_hz, float sample_rate_hz);
float lowpass_process(lowpass_filter_t *filter, float input);
void lowpass_reset(lowpass_filter_t *filter);

/* Moving Average Filter */
void moving_avg_init(moving_average_filter_t *filter, uint32_t size);
float moving_avg_process(moving_average_filter_t *filter, float input);
void moving_avg_reset(moving_average_filter_t *filter);

#ifdef __cplusplus
}
#endif

#endif /* DIGITAL_FILTER_H */
