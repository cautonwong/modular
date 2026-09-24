#include "digital_filter/digital_filter.h"
#include <math.h>
#include <string.h>

#define M_PI_F 3.14159265358979323846f

/*
 * Reference firmware: util/digital_filter.c biquad_config / biquad_process /
 * biquad_reset, copied unchanged. Fc is already normalised to the sample rate and
 * Q is fixed at 0.707, so this is not the same filter as an RBJ biquad configured
 * with a cutoff in Hz and a Q you pick yourself: at fs = 25 kHz, fc = 1 kHz the
 * two impulse responses differ, and the reference's is the one the IMU expects.
 */
void biquad_config(biquad_filter_t *filter, biquad_type_t type, float fc_normalized) {
    if (filter == (void *)0) {
        return;
    }

    float k = tanf(M_PI_F * fc_normalized);
    const float q = 0.707f; /* maximum sharpness (0.5 = maximum smoothness) */
    float norm = 1.0f / (1.0f + k / q + k * k);

    if (type == BQ_LOWPASS) {
        filter->a0 = k * k * norm;
        filter->a1 = 2.0f * filter->a0;
        filter->a2 = filter->a0;
    } else if (type == BQ_HIGHPASS) {
        filter->a0 = 1.0f * norm;
        filter->a1 = -2.0f * filter->a0;
        filter->a2 = filter->a0;
    }

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

void lowpass_init(lowpass_filter_t *filter, float cutoff_hz, float sample_rate_hz) {
    if (!filter) {
        return;
    }
    memset(filter, 0, sizeof(*filter));
    filter->cutoff_hz = cutoff_hz;
    filter->sample_rate_hz = sample_rate_hz;

    if (cutoff_hz > 0.0f && sample_rate_hz > 0.0f) {
        float rc = 1.0f / (2.0f * M_PI_F * cutoff_hz);
        float dt = 1.0f / sample_rate_hz;
        filter->alpha = dt / (rc + dt);
    } else {
        filter->alpha = 1.0f;
    }
}

float lowpass_process(lowpass_filter_t *filter, float input) {
    if (!filter) {
        return input;
    }
    if (!filter->initialized) {
        filter->prev_output = input;
        filter->initialized = true;
        return input;
    }
    filter->prev_output = filter->prev_output + filter->alpha * (input - filter->prev_output);
    return filter->prev_output;
}

void lowpass_reset(lowpass_filter_t *filter) {
    if (filter) {
        filter->prev_output = 0.0f;
        filter->initialized = false;
    }
}

void moving_avg_init(moving_average_filter_t *filter, uint32_t size) {
    if (!filter) {
        return;
    }
    memset(filter, 0, sizeof(*filter));
    if (size == 0 || size > 32) {
        size = 8;
    }
    filter->size = size;
}

float moving_avg_process(moving_average_filter_t *filter, float input) {
    if (!filter || filter->size == 0) {
        return input;
    }

    if (filter->count < filter->size) {
        filter->buffer[filter->head] = input;
        filter->sum += input;
        filter->count++;
        filter->head = (filter->head + 1) % filter->size;
        return filter->sum / (float)filter->count;
    }

    filter->sum -= filter->buffer[filter->head];
    filter->buffer[filter->head] = input;
    filter->sum += input;
    filter->head = (filter->head + 1) % filter->size;

    return filter->sum / (float)filter->size;
}

void moving_avg_reset(moving_average_filter_t *filter) {
    if (filter) {
        uint32_t s = filter->size;
        memset(filter, 0, sizeof(*filter));
        filter->size = s;
    }
}
