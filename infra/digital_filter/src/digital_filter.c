#include "digital_filter/digital_filter.h"
#include <math.h>
#include <string.h>

#define M_PI_F 3.14159265358979323846f

void biquad_init_lowpass(biquad_filter_t *filter, float sample_rate, float cutoff_freq, float q) {
    if (!filter || sample_rate <= 0.0f || cutoff_freq <= 0.0f) {
        return;
    }
    if (q <= 0.0f) {
        q = 0.7071f;
    }

    float omega = 2.0f * M_PI_F * cutoff_freq / sample_rate;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / (2.0f * q);

    float a0 = 1.0f + alpha;
    filter->b0 = ((1.0f - cs) / 2.0f) / a0;
    filter->b1 = (1.0f - cs) / a0;
    filter->b2 = ((1.0f - cs) / 2.0f) / a0;
    filter->a1 = (-2.0f * cs) / a0;
    filter->a2 = (1.0f - alpha) / a0;

    biquad_reset(filter);
}

void biquad_init_notch(biquad_filter_t *filter, float sample_rate, float center_freq, float q) {
    if (!filter || sample_rate <= 0.0f || center_freq <= 0.0f) {
        return;
    }
    if (q <= 0.0f) {
        q = 1.0f;
    }

    float omega = 2.0f * M_PI_F * center_freq / sample_rate;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / (2.0f * q);

    float a0 = 1.0f + alpha;
    filter->b0 = 1.0f / a0;
    filter->b1 = (-2.0f * cs) / a0;
    filter->b2 = 1.0f / a0;
    filter->a1 = (-2.0f * cs) / a0;
    filter->a2 = (1.0f - alpha) / a0;

    biquad_reset(filter);
}

float biquad_process(biquad_filter_t *filter, float input) {
    if (!filter) {
        return input;
    }

    float out = filter->b0 * input + filter->b1 * filter->x1 + filter->b2 * filter->x2 -
                filter->a1 * filter->y1 - filter->a2 * filter->y2;

    filter->x2 = filter->x1;
    filter->x1 = input;
    filter->y2 = filter->y1;
    filter->y1 = out;

    return out;
}

void biquad_reset(biquad_filter_t *filter) {
    if (filter) {
        filter->x1 = 0.0f;
        filter->x2 = 0.0f;
        filter->y1 = 0.0f;
        filter->y2 = 0.0f;
    }
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
