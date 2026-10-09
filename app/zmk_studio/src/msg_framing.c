#include "zmk_studio/studio.h"

bool zmk_studio_framing_process_byte(zmk_studio_framing_state_t *state, uint8_t byte,
                                     uint8_t *out_data) {
    if (state == NULL) {
        return false;
    }

    switch (*state) {
    case ZMK_STUDIO_STATE_IDLE:
        if (byte == ZMK_STUDIO_FRAMING_SOF) {
            *state = ZMK_STUDIO_STATE_AWAITING_DATA;
        }
        return false;

    case ZMK_STUDIO_STATE_AWAITING_DATA:
        if (byte == ZMK_STUDIO_FRAMING_SOF) {
            // New frame started
            return false;
        } else if (byte == ZMK_STUDIO_FRAMING_EOF) {
            *state = ZMK_STUDIO_STATE_EOF;
            return false;
        } else if (byte == ZMK_STUDIO_FRAMING_ESC) {
            *state = ZMK_STUDIO_STATE_ESCAPED;
            return false;
        } else {
            if (out_data != NULL) {
                *out_data = byte;
            }
            return true;
        }

    case ZMK_STUDIO_STATE_ESCAPED:
        *state = ZMK_STUDIO_STATE_AWAITING_DATA;
        if (out_data != NULL) {
            *out_data = byte;
        }
        return true;

    case ZMK_STUDIO_STATE_ERR:
    case ZMK_STUDIO_STATE_EOF:
        if (byte == ZMK_STUDIO_FRAMING_SOF) {
            *state = ZMK_STUDIO_STATE_AWAITING_DATA;
        } else {
            *state = ZMK_STUDIO_STATE_IDLE;
        }
        return false;
    }

    return false;
}

size_t zmk_studio_frame_encode(const uint8_t *in_data, size_t in_len, uint8_t *out_frame,
                               size_t out_max_len) {
    if (in_data == NULL || out_frame == NULL || out_max_len < 2) {
        return 0;
    }

    size_t out_idx = 0;
    out_frame[out_idx++] = ZMK_STUDIO_FRAMING_SOF;

    for (size_t i = 0; i < in_len; i++) {
        uint8_t b = in_data[i];
        if (b == ZMK_STUDIO_FRAMING_SOF || b == ZMK_STUDIO_FRAMING_ESC ||
            b == ZMK_STUDIO_FRAMING_EOF) {
            if (out_idx + 2 >= out_max_len) {
                return 0; // Buffer overflow
            }
            out_frame[out_idx++] = ZMK_STUDIO_FRAMING_ESC;
            out_frame[out_idx++] = b;
        } else {
            if (out_idx + 1 >= out_max_len) {
                return 0;
            }
            out_frame[out_idx++] = b;
        }
    }

    if (out_idx >= out_max_len) {
        return 0;
    }
    out_frame[out_idx++] = ZMK_STUDIO_FRAMING_EOF;
    return out_idx;
}
