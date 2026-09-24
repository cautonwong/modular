#include "vesc_buffer/buffer.h"
#include <math.h>

void vesc_buffer_append_int16(uint8_t *buffer, int16_t number, int32_t *index) {
    buffer[(*index)++] = (uint8_t)((uint16_t)number >> 8);
    buffer[(*index)++] = (uint8_t)number;
}

void vesc_buffer_append_uint16(uint8_t *buffer, uint16_t number, int32_t *index) {
    buffer[(*index)++] = (uint8_t)(number >> 8);
    buffer[(*index)++] = (uint8_t)number;
}

void vesc_buffer_append_int32(uint8_t *buffer, int32_t number, int32_t *index) {
    buffer[(*index)++] = (uint8_t)((uint32_t)number >> 24);
    buffer[(*index)++] = (uint8_t)((uint32_t)number >> 16);
    buffer[(*index)++] = (uint8_t)((uint32_t)number >> 8);
    buffer[(*index)++] = (uint8_t)number;
}

void vesc_buffer_append_uint32(uint8_t *buffer, uint32_t number, int32_t *index) {
    buffer[(*index)++] = (uint8_t)(number >> 24);
    buffer[(*index)++] = (uint8_t)(number >> 16);
    buffer[(*index)++] = (uint8_t)(number >> 8);
    buffer[(*index)++] = (uint8_t)number;
}

void vesc_buffer_append_int64(uint8_t *buffer, int64_t number, int32_t *index) {
    uint64_t unum = (uint64_t)number;
    buffer[(*index)++] = (uint8_t)(unum >> 56);
    buffer[(*index)++] = (uint8_t)(unum >> 48);
    buffer[(*index)++] = (uint8_t)(unum >> 40);
    buffer[(*index)++] = (uint8_t)(unum >> 32);
    buffer[(*index)++] = (uint8_t)(unum >> 24);
    buffer[(*index)++] = (uint8_t)(unum >> 16);
    buffer[(*index)++] = (uint8_t)(unum >> 8);
    buffer[(*index)++] = (uint8_t)unum;
}

void vesc_buffer_append_uint64(uint8_t *buffer, uint64_t number, int32_t *index) {
    buffer[(*index)++] = (uint8_t)(number >> 56);
    buffer[(*index)++] = (uint8_t)(number >> 48);
    buffer[(*index)++] = (uint8_t)(number >> 40);
    buffer[(*index)++] = (uint8_t)(number >> 32);
    buffer[(*index)++] = (uint8_t)(number >> 24);
    buffer[(*index)++] = (uint8_t)(number >> 16);
    buffer[(*index)++] = (uint8_t)(number >> 8);
    buffer[(*index)++] = (uint8_t)number;
}

void vesc_buffer_append_float16(uint8_t *buffer, float number, float scale, int32_t *index) {
    vesc_buffer_append_int16(buffer, (int16_t)(number * scale), index);
}

void vesc_buffer_append_float32(uint8_t *buffer, float number, float scale, int32_t *index) {
    vesc_buffer_append_int32(buffer, (int32_t)(number * scale), index);
}

void vesc_buffer_append_double64(uint8_t *buffer, double number, double scale, int32_t *index) {
    vesc_buffer_append_int64(buffer, (int64_t)(number * scale), index);
}

void vesc_buffer_append_float32_auto(uint8_t *buffer, float number, int32_t *index) {
    int e = 0;
    float sig = frexpf(number, &e);
    float sig_scaled = sig * 8388608.0f; /* 2^23 */
    int32_t sig_int = (int32_t)sig_scaled;
    int32_t res = ((e & 0xFF) << 23) | (sig_int & 0x7FFFFF);
    if (number < 0.0f) {
        res |= (int32_t)0x80000000u;
    }
    vesc_buffer_append_int32(buffer, res, index);
}

int16_t vesc_buffer_get_int16(const uint8_t *buffer, int32_t *index) {
    int16_t res = (int16_t)(((uint16_t)buffer[*index] << 8) | ((uint16_t)buffer[*index + 1]));
    *index += 2;
    return res;
}

uint16_t vesc_buffer_get_uint16(const uint8_t *buffer, int32_t *index) {
    uint16_t res = ((uint16_t)buffer[*index] << 8) | ((uint16_t)buffer[*index + 1]);
    *index += 2;
    return res;
}

int32_t vesc_buffer_get_int32(const uint8_t *buffer, int32_t *index) {
    int32_t res =
        (int32_t)(((uint32_t)buffer[*index] << 24) | ((uint32_t)buffer[*index + 1] << 16) |
                  ((uint32_t)buffer[*index + 2] << 8) | ((uint32_t)buffer[*index + 3]));
    *index += 4;
    return res;
}

uint32_t vesc_buffer_get_uint32(const uint8_t *buffer, int32_t *index) {
    uint32_t res = ((uint32_t)buffer[*index] << 24) | ((uint32_t)buffer[*index + 1] << 16) |
                   ((uint32_t)buffer[*index + 2] << 8) | ((uint32_t)buffer[*index + 3]);
    *index += 4;
    return res;
}

int64_t vesc_buffer_get_int64(const uint8_t *buffer, int32_t *index) {
    int64_t res =
        (int64_t)(((uint64_t)buffer[*index] << 56) | ((uint64_t)buffer[*index + 1] << 48) |
                  ((uint64_t)buffer[*index + 2] << 40) | ((uint64_t)buffer[*index + 3] << 32) |
                  ((uint64_t)buffer[*index + 4] << 24) | ((uint64_t)buffer[*index + 5] << 16) |
                  ((uint64_t)buffer[*index + 6] << 8) | ((uint64_t)buffer[*index + 7]));
    *index += 8;
    return res;
}

uint64_t vesc_buffer_get_uint64(const uint8_t *buffer, int32_t *index) {
    uint64_t res = ((uint64_t)buffer[*index] << 56) | ((uint64_t)buffer[*index + 1] << 48) |
                   ((uint64_t)buffer[*index + 2] << 40) | ((uint64_t)buffer[*index + 3] << 32) |
                   ((uint64_t)buffer[*index + 4] << 24) | ((uint64_t)buffer[*index + 5] << 16) |
                   ((uint64_t)buffer[*index + 6] << 8) | ((uint64_t)buffer[*index + 7]);
    *index += 8;
    return res;
}

float vesc_buffer_get_float16(const uint8_t *buffer, float scale, int32_t *index) {
    return (float)vesc_buffer_get_int16(buffer, index) / scale;
}

float vesc_buffer_get_float32(const uint8_t *buffer, float scale, int32_t *index) {
    return (float)vesc_buffer_get_int32(buffer, index) / scale;
}

double vesc_buffer_get_double64(const uint8_t *buffer, double scale, int32_t *index) {
    return (double)vesc_buffer_get_int64(buffer, index) / scale;
}

float vesc_buffer_get_float32_auto(const uint8_t *buffer, int32_t *index) {
    int32_t res = vesc_buffer_get_int32(buffer, index);
    int e = (int)((res >> 23) & 0xFF);
    if (e & 0x80) {
        e -= 256;
    }
    int32_t sig_int = res & 0x7FFFFF;
    if (res & (int32_t)0x80000000u) {
        sig_int = -sig_int;
    }
    float sig = (float)sig_int / 8388608.0f;
    return ldexpf(sig, e);
}
