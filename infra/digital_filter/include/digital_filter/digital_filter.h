#ifndef DIGITAL_FILTER_H
#define DIGITAL_FILTER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Direct form II transposed, with the coefficient set the reference firmware uses
 * (util/digital_filter.c biquad_config): K = tan(pi * Fc) with Fc already
 * normalised to the sample rate, Q fixed at 0.707. Ported as-is because the
 * IMU's accelerometer and gyro paths filter through exactly this response.
 */
typedef enum {
    BQ_LOWPASS = 0,
    BQ_HIGHPASS,
} biquad_type_t;

typedef struct biquad_filter {
    float a0, a1, a2;
    float b1, b2;
    float z1, z2;
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
void biquad_config(biquad_filter_t *filter, biquad_type_t type, float fc_normalized);
void biquad_reset(biquad_filter_t *filter);
float biquad_process(biquad_filter_t *filter, float input);

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
