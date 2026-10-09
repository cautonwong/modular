#ifndef INFRA_RLE_H
#define INFRA_RLE_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EDGE_EOF EDGE_ENOENT

typedef struct rle_decoder {
    const uint8_t *buffer;
    size_t size;
    size_t encoded_index;
    uint16_t foreground_color;
    uint16_t background_color;
    uint16_t current_color;
    uint16_t current_bp;
    int processed_count;
    bool is_finished;
} rle_decoder_t;

void rle_decoder_init(rle_decoder_t *self, const uint8_t *buffer, size_t size,
                      uint16_t foreground_color, uint16_t background_color);

/**
 * Decode next chunk of 1-bit RLE compressed graphic data into RGB565 buffer.
 *
 * @param self Decoder instance pointer.
 * @param output Destination buffer for 16-bit RGB565 pixel data (2 bytes per pixel).
 * @param max_bytes Capacity of output buffer in bytes (must be even).
 * @param out_written Bytes written to output buffer.
 * @return EDGE_OK on success, EDGE_EOF if entire stream has been decoded.
 */
edge_status_t rle_decoder_decode_next(rle_decoder_t *self, uint8_t *output, size_t max_bytes,
                                      size_t *out_written);

#ifdef __cplusplus
}
#endif

#endif /* INFRA_RLE_H */
