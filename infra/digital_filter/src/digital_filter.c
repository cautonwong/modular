#include "digital_filter/digital_filter.h"
#include <math.h>

#define M_PI_F 3.14159265358979323846f

/*
 * Reference firmware: util/digital_filter.c biquad_config / biquad_process /
 * biquad_reset, lowpass branch. Fc is already normalised to the sample rate and
 * Q is fixed at 0.707, so this is not the same filter as an RBJ biquad configured
 * with a cutoff in Hz and a Q you pick yourself: at fs = 25 kHz, fc = 1 kHz the
 * two impulse responses differ, and the reference's is the one the IMU expects.
 */
void biquad_config(biquad_filter_t *filter, float fc_normalized) {
    if (filter == (void *)0) {
        return;
    }

    float k = tanf(M_PI_F * fc_normalized);
    const float q = 0.707f; /* maximum sharpness (0.5 = maximum smoothness) */
    float norm = 1.0f / (1.0f + k / q + k * k);

    filter->a0 = k * k * norm;
    filter->a1 = 2.0f * filter->a0;
    filter->a2 = filter->a0;
    filter->b1 = 2.0f * (k * k - 1.0f) * norm;
    filter->b2 = (1.0f - k / q + k * k) * norm;

    biquad_reset(filter);
}

void biquad_reset(biquad_filter_t *filter) {
    if (filter == (void *)0) {
        return;
    }
    filter->z1 = 0.0f;
    filter->z2 = 0.0f;
}

float biquad_process(biquad_filter_t *filter, float input) {
    if (filter == (void *)0) {
        return input;
    }

    float out = input * filter->a0 + filter->z1;
    filter->z1 = input * filter->a1 + filter->z2 - filter->b1 * out;
    filter->z2 = input * filter->a2 - filter->b2 * out;
    return out;
}
