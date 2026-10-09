#include "heart_rate/heart_rate.h"

#include "edge/events.h"
#define PPG_DELTA_T_MS 100u
#define PPG_SAMPLE_FREQ (1000.0f / (float)PPG_DELTA_T_MS)       /* 10.0 Hz */
#define PPG_FREQ_RES (PPG_SAMPLE_FREQ / (float)PPG_DATA_LENGTH) /* 0.15625 Hz */
#define PPG_PEAK_THRESHOLD 0.6f
#define PPG_MAX_PEAK_WIDTH 2.5f
#define PPG_SNR_THRESHOLD 3.0f
#define PPG_MIN_HR (40.0f / 60.0f)  /* 0.667 Hz */
#define PPG_MAX_HR (230.0f / 60.0f) /* 3.833 Hz */
#define PPG_DC_THRESHOLD 0.5f
#define PPG_ALS_FACTOR 2.0f
#define PPG_HR_ROI_BEGIN 3u /* ~30 BPM */
#define PPG_HR_ROI_END 26u  /* ~240 BPM */

static const float s_hanning[PPG_SPECTRUM_LENGTH] = {
    0.0f,        0.00248461f, 0.00991376f, 0.0222136f,  0.03926189f, 0.06088921f, 0.08688061f,
    0.11697778f, 0.15088159f, 0.1882551f,  0.22872687f, 0.27189467f, 0.31732949f, 0.36457977f,
    0.41317591f, 0.46263495f, 0.51246535f, 0.56217185f, 0.61126047f, 0.65924333f, 0.70564355f,
    0.75f,       0.79187184f, 0.83084292f, 0.86652594f, 0.89856625f, 0.92664544f, 0.95048443f,
    0.96984631f, 0.98453864f, 0.99441541f, 0.99937846f};

static void detrend(float *signal, size_t size) {
    const float offset = signal[0];
    const float slope = (signal[size - 1] - offset) / (float)(size - 1);
    for (size_t i = 0; i < size; i++) {
        signal[i] -= (slope * (float)i + offset);
    }
    for (size_t i = 0; i < size - 1; i++) {
        signal[i] = signal[i + 1] - signal[i];
    }
}

static void filter_30_to_240(float *signal, size_t size) {
    float exp_alpha = 0.816f;
    float exp_avg;
    for (int loop = 0; loop < 4; loop++) {
        exp_avg = signal[0];
        for (size_t i = 0; i < size; i++) {
            exp_avg = (exp_alpha * signal[i]) + ((1.0f - exp_alpha) * exp_avg);
            signal[i] = exp_avg;
        }
    }
    exp_alpha = 0.268f;
    for (int loop = 0; loop < 4; loop++) {
        exp_avg = signal[0];
        for (size_t i = 0; i < size; i++) {
            exp_avg = (exp_alpha * signal[i]) + ((1.0f - exp_alpha) * exp_avg);
            signal[i] -= exp_avg;
        }
    }
}

static float hr_sqrtf(float x) {
    if (x <= 0.0f) {
        return 0.0f;
    }
    float guess = x > 1.0f ? x : 1.0f;
    for (int i = 0; i < 10; i++) {
        guess = 0.5f * (guess + x / guess);
    }
    return guess;
}

static void get_twiddle(uint32_t len, float *out_r, float *out_i) {
    switch (len) {
    case 2u:
        *out_r = -1.0f;
        *out_i = 0.0f;
        break;
    case 4u:
        *out_r = 0.0f;
        *out_i = -1.0f;
        break;
    case 8u:
        *out_r = 0.7071067811865476f;
        *out_i = -0.7071067811865476f;
        break;
    case 16u:
        *out_r = 0.9238795325112867f;
        *out_i = -0.3826834323650898f;
        break;
    case 32u:
        *out_r = 0.9807852804032304f;
        *out_i = -0.19509032201612825f;
        break;
    case 64u:
        *out_r = 0.9951847266721969f;
        *out_i = -0.0980171403295606f;
        break;
    default:
        *out_r = 1.0f;
        *out_i = 0.0f;
        break;
    }
}

static void fft_64(float *v_real, float *v_imag) {
    for (uint32_t i = 0; i < 64u; i++) {
        uint32_t j = 0;
        for (uint32_t b = 0; b < 6u; b++) {
            if (i & (1u << b)) {
                j |= (1u << (5u - b));
            }
        }
        if (i < j) {
            float tr = v_real[i];
            v_real[i] = v_real[j];
            v_real[j] = tr;
            float ti = v_imag[i];
            v_imag[i] = v_imag[j];
            v_imag[j] = ti;
        }
    }

    for (uint32_t len = 2; len <= 64; len <<= 1) {
        float wlen_r = 1.0f;
        float wlen_i = 0.0f;
        get_twiddle(len, &wlen_r, &wlen_i);
        for (uint32_t i = 0; i < 64; i += len) {
            float w_r = 1.0f;
            float w_i = 0.0f;
            for (uint32_t j = 0; j < len / 2; j++) {
                uint32_t u = i + j;
                uint32_t v = i + j + len / 2;
                float u_r = v_real[u];
                float u_i = v_imag[u];
                float v_r = v_real[v] * w_r - v_imag[v] * w_i;
                float v_i = v_real[v] * w_i + v_imag[v] * w_r;
                v_real[u] = u_r + v_r;
                v_imag[u] = u_i + v_i;
                v_real[v] = u_r - v_r;
                v_imag[v] = u_i - v_i;
                float next_w_r = w_r * wlen_r - w_i * wlen_i;
                float next_w_i = w_r * wlen_i + w_i * wlen_r;
                w_r = next_w_r;
                w_i = next_w_i;
            }
        }
    }

    for (uint32_t i = 0; i < 32; i++) {
        v_real[i] = hr_sqrtf(v_real[i] * v_real[i] + v_imag[i] * v_imag[i]);
    }
}

static float linear_interpolation(const float *x_vals, const float *y_vals, int length,
                                  float point_x) {
    if (point_x > x_vals[length - 1]) {
        return y_vals[length - 1];
    }
    if (point_x <= x_vals[0]) {
        return y_vals[0];
    }
    int idx = 0;
    while (idx < length - 1 && point_x > x_vals[idx]) {
        idx++;
    }
    float x0 = x_vals[idx - 1];
    float x1 = x_vals[idx];
    float y0 = y_vals[idx - 1];
    float y1 = y_vals[idx];
    float mu = (point_x - x0) / (x1 - x0);
    return (y0 * (1.0f - mu) + y1 * mu);
}

static float peak_search(const float *x_vals, const float *y_vals, float threshold,
                         float *out_width, float start, float end, int length) {
    int peaks = 0;
    bool enabled = false;
    float min_bin = 0.0f;
    float peak_center = 0.0f;
    float prev_val = linear_interpolation(x_vals, y_vals, length, start - 0.01f);
    float curr_val = linear_interpolation(x_vals, y_vals, length, start);
    float idx = start;
    while (idx < end) {
        float next_val = linear_interpolation(x_vals, y_vals, length, idx + 0.01f);
        if (curr_val < threshold) {
            enabled = true;
        }
        if (curr_val >= threshold && enabled) {
            if (prev_val < threshold) {
                min_bin = idx;
            } else if (next_val <= threshold) {
                float max_bin = idx;
                peaks++;
                *out_width = max_bin - min_bin;
                peak_center = (*out_width) / 2.0f + min_bin;
            }
        }
        prev_val = curr_val;
        curr_val = next_val;
        idx += 0.01f;
    }
    if (peaks != 1) {
        *out_width = 0.0f;
        peak_center = 0.0f;
    }
    return peak_center;
}

static float spectrum_mean(const float *signal, uint32_t start, uint32_t end) {
    float sum = 0.0f;
    uint32_t count = 0;
    for (uint32_t i = start; i < end; i++) {
        sum += signal[i];
        count++;
    }
    return count > 0 ? (sum / (float)count) : 0.0f;
}

static float spectrum_max(const float *signal, uint32_t start, uint32_t end) {
    float max_val = 0.0f;
    for (uint32_t i = start; i < end; i++) {
        if (signal[i] > max_val) {
            max_val = signal[i];
        }
    }
    return max_val;
}

static float signal_to_noise(const float *signal, uint32_t start, uint32_t end, float max_val) {
    float mean = spectrum_mean(signal, start, end);
    return mean > 1e-6f ? (max_val / mean) : 0.0f;
}

static void spectrum_average(const float *data, float *spectrum, uint32_t length,
                             uint16_t *avg_count, bool reset) {
    if (reset) {
        *avg_count = 0;
    }
    float count = (float)(*avg_count);
    for (uint32_t i = 0; i < length; i++) {
        spectrum[i] = (spectrum[i] * count + data[i]) / (count + 1.0f);
    }
    if (*avg_count < PPG_SPECTRAL_AVG_MAX) {
        (*avg_count)++;
    }
}

static float heart_rate_average(heart_rate_t *self, float hr) {
    self->avg_index = (self->avg_index + 1) % PPG_AVG_HISTORY_LEN;
    self->data_average[self->avg_index] = hr;

    float sum = 0.0f;
    float count = 0.0f;
    for (uint32_t i = 0; i < PPG_AVG_HISTORY_LEN; i++) {
        if (self->data_average[i] > 0.0f) {
            sum += self->data_average[i];
            count += 1.0f;
        }
    }
    return count > 0.0f ? (sum / count) : 0.0f;
}

static edge_status_t heart_rate_poll(edge_module_t *module) {
    heart_rate_t *self = (heart_rate_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->poll_count++;

    if (!self->is_running || self->sensor == NULL || self->sensor->read_sample == NULL) {
        return EDGE_OK;
    }

    uint16_t hrs = 0;
    uint16_t als = 0;
    if (self->sensor->read_sample(self->sensor->self, &hrs, &als) == EDGE_OK) {
        int bpm = 0;
        (void)heart_rate_process_sample(self, hrs, als, &bpm);
    }
    return EDGE_OK;
}

static edge_status_t heart_rate_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t heart_rate_power_off(edge_module_t *module) {
    heart_rate_t *self = (heart_rate_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return heart_rate_shutdown(self);
}

void heart_rate_construct(heart_rate_t *self, uint32_t module_id, uint32_t priority,
                          const ppg_sensor_if_t *sensor) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .budget = 0u,
        .next_due = 0u,
        .poll = heart_rate_poll,
        .on_event = heart_rate_on_event,
        .power_off = heart_rate_power_off,
        .private_data = self,
    };
    self->sensor = sensor;
    self->reset_spectral_avg = true;
    self->als_threshold = UINT16_MAX;
}

edge_status_t heart_rate_init(heart_rate_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->data_index = 0;
    self->enough_data = false;
    self->reset_spectral_avg = true;
    self->spectral_avg_count = 0;
    self->avg_index = 0;
    self->last_peak_location = 0.0f;
    self->als_threshold = UINT16_MAX;
    self->als_value = 0;
    self->current_bpm = 0;
    self->is_running = true;
    self->poll_count = 0;

    if (self->sensor != NULL && self->sensor->enable != NULL) {
        return self->sensor->enable(self->sensor->self, true);
    }
    return EDGE_OK;
}

edge_status_t heart_rate_shutdown(heart_rate_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->is_running = false;
    if (self->sensor != NULL && self->sensor->enable != NULL) {
        return self->sensor->enable(self->sensor->self, false);
    }
    return EDGE_OK;
}

edge_status_t heart_rate_process_sample(heart_rate_t *self, uint16_t hrs, uint16_t als,
                                        int *out_bpm) {
    if (self == NULL || out_bpm == NULL) {
        return EDGE_EINVAL;
    }

    if (self->data_index < PPG_DATA_LENGTH) {
        self->data_hrs[self->data_index++] = hrs;
    }
    self->als_value = als;

    if (self->data_index < PPG_DATA_LENGTH) {
        if (!self->enough_data) {
            *out_bpm = -2; /* Not enough data yet */
            return EDGE_OK;
        }
        *out_bpm = 0;
        return EDGE_OK;
    }

    self->enough_data = true;

    /* Copy raw data to float buffer */
    for (size_t i = 0; i < PPG_DATA_LENGTH; i++) {
        self->v_real[i] = (float)self->data_hrs[i];
    }
    detrend(self->v_real, PPG_DATA_LENGTH);
    filter_30_to_240(self->v_real, PPG_DATA_LENGTH);
    for (size_t i = 0; i < PPG_DATA_LENGTH; ++i) {
        self->v_imag[i] = 0.0f;
    }

    /* Apply Hanning Window */
    int hann_idx = 0;
    for (size_t i = 0; i < PPG_DATA_LENGTH; i++) {
        if (i >= (PPG_DATA_LENGTH >> 1u)) {
            hann_idx--;
        }
        self->v_real[i] *= s_hanning[hann_idx];
        if (i < (PPG_DATA_LENGTH >> 1u)) {
            hann_idx++;
        }
    }

    /* Compute FFT */
    fft_64(self->v_real, self->v_imag);

    spectrum_average(self->v_real, self->spectrum, PPG_SPECTRUM_LENGTH, &self->spectral_avg_count,
                     self->reset_spectral_avg);
    self->reset_spectral_avg = false;

    float peak_loc = 0.0f;
    float peak_width = 0.0f;
    float max_power = spectrum_max(self->spectrum, PPG_HR_ROI_BEGIN, PPG_HR_ROI_END);
    float snr = signal_to_noise(self->spectrum, PPG_HR_ROI_BEGIN, PPG_HR_ROI_END, max_power);

    if (snr > PPG_SNR_THRESHOLD && self->spectrum[0] < PPG_DC_THRESHOLD) {
        float threshold = PPG_PEAK_THRESHOLD * max_power;
        for (size_t i = 0; i < PPG_DATA_LENGTH; i++) {
            self->v_imag[i] = (float)i;
        }
        peak_loc = peak_search(self->v_imag, self->spectrum, threshold, &peak_width,
                               (float)PPG_HR_ROI_BEGIN, (float)PPG_HR_ROI_END, PPG_SPECTRUM_LENGTH);
        peak_loc *= PPG_FREQ_RES;
    }

    if (peak_width > PPG_MAX_PEAK_WIDTH || peak_loc < PPG_MIN_HR || peak_loc > PPG_MAX_HR) {
        peak_loc = 0.0f;
        self->reset_spectral_avg = true;
    }

    self->als_threshold = (uint16_t)((float)self->als_value * PPG_ALS_FACTOR);
    peak_loc = heart_rate_average(self, peak_loc);

    int rtn = -1;
    if (peak_loc == 0.0f && self->last_peak_location > 0.0f) {
        self->last_peak_location = 0.0f;
    } else {
        self->last_peak_location = peak_loc;
        rtn = (int)(peak_loc * 60.0f + 0.5f);
    }

    if (rtn > 0) {
        self->current_bpm = (uint8_t)rtn;
    }

    /* Slide data buffer by overlap window */
    for (size_t i = 0; i < PPG_DATA_LENGTH - PPG_OVERLAP_WINDOW; i++) {
        self->data_hrs[i] = self->data_hrs[i + PPG_OVERLAP_WINDOW];
    }
    self->data_index = PPG_DATA_LENGTH - PPG_OVERLAP_WINDOW;

    *out_bpm = rtn;
    return EDGE_OK;
}

uint8_t heart_rate_get_bpm(const heart_rate_t *self) {
    return self != NULL ? self->current_bpm : 0;
}
