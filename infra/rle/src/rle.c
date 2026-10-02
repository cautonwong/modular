#include "rle/rle.h"

void rle_decoder_init(rle_decoder_t *self, const uint8_t *buffer, size_t size,
                      uint16_t foreground_color, uint16_t background_color) {
    if (self == NULL) {
        return;
    }
    self->buffer = buffer;
    self->size = size;
    self->encoded_index = 0u;
    self->foreground_color = foreground_color;
    self->background_color = background_color;
    self->current_color = background_color;
    self->current_bp = 0u;
    self->processed_count = 0;
    self->is_finished = (buffer == NULL || size == 0u);
}

edge_status_t rle_decoder_decode_next(rle_decoder_t *self, uint8_t *output, size_t max_bytes,
                                      size_t *out_written) {
    if (self == NULL || output == NULL || max_bytes < 2u) {
        return EDGE_EINVAL;
    }

    if (self->is_finished || self->buffer == NULL) {
        if (out_written != NULL) {
            *out_written = 0u;
        }
        return EDGE_EOF;
    }

    size_t bp = 0u;

    for (; self->encoded_index < self->size; self->encoded_index++) {
        int rl = (int)self->buffer[self->encoded_index] - self->processed_count;
        while (rl > 0) {
            output[bp] = (uint8_t)(self->current_color >> 8u);
            output[bp + 1u] = (uint8_t)(self->current_color & 0xFFu);
            bp += 2u;
            rl -= 1;
            self->processed_count++;

            if (bp >= max_bytes) {
                if (out_written != NULL) {
                    *out_written = bp;
                }
                return EDGE_OK;
            }
        }
        self->processed_count = 0;

        if (self->current_color == self->background_color) {
            self->current_color = self->foreground_color;
        } else {
            self->current_color = self->background_color;
        }
    }

    self->is_finished = true;
    if (out_written != NULL) {
        *out_written = bp;
    }
    return (bp > 0u) ? EDGE_OK : EDGE_EOF;
}
