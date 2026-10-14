#ifndef DIGITAL_FILTER_H
#define DIGITAL_FILTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Direct form II transposed, with the coefficient set the reference firmware uses
 * (util/digital_filter.c biquad_config): K = tan(pi * Fc) with Fc already
 * normalised to the sample rate, Q fixed at 0.707. Ported as-is because the
 * IMU's accelerometer and gyro paths filter through exactly this response.
 *
 * Only the lowpass is kept: the reference also has a highpass branch, but its own
 * IMU (the only consumer of biquads in the whole tree) never selects it. Adding
 * it back is four lines if a caller ever needs it.
 */
typedef struct biquad_filter {
    float a0, a1, a2;
    float b1, b2;
    float z1, z2;
} biquad_filter_t;

void biquad_config(biquad_filter_t *filter, float fc_normalized);
void biquad_reset(biquad_filter_t *filter);
float biquad_process(biquad_filter_t *filter, float input);

#ifdef __cplusplus
}
#endif

#endif /* DIGITAL_FILTER_H */
