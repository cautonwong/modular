#include "hrs3300/hrs3300.h"

edge_status_t hrs3300_unpack_burst(const uint8_t burst_buf[8], hrs3300_sample_t *out_sample) {
    if (burst_buf == NULL || out_sample == NULL) {
        return EDGE_EINVAL;
    }

    /*
     * buf layout starting from base reg 0x08:
     * buf[0] = C1DataM (0x08)
     * buf[1] = C0DataM (0x09)
     * buf[2] = C0DataH (0x0A)
     * buf[3] = (0x0B)
     * buf[4] = PDriver (0x0C)
     * buf[5] = C1DataH (0x0D)
     * buf[6] = C1DataL (0x0E)
     * buf[7] = C0DataL (0x0F)
     */
    const uint8_t m_hrs = burst_buf[1];
    const uint8_t h_hrs = burst_buf[2];
    const uint8_t l_hrs = burst_buf[7];

    const uint8_t m_als = burst_buf[0];
    const uint8_t h_als = burst_buf[5];
    const uint8_t l_als = burst_buf[6];

    out_sample->hrs = (uint16_t)(((uint16_t)m_hrs << 8) | (((uint16_t)h_hrs & 0x0Fu) << 4) |
                                 ((uint16_t)l_hrs & 0x0Fu));

    out_sample->als = (uint16_t)((((uint16_t)h_als & 0x3Fu) << 11) | ((uint16_t)m_als << 3) |
                                 ((uint16_t)l_als & 0x07u));

    return EDGE_OK;
}
