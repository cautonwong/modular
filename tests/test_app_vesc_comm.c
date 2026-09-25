/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include <cmocka.h>
/* clang-format on */

#include "edge/errors.h"
#include "edge/modules.h"
#include "vesc_comm/vesc_comm.h"

typedef struct mock_comm_ctx {
    uint8_t tx_buf[1024];
    size_t tx_len;
    size_t tx_count;

    vesc_values_t current_values;
    float set_duty_val;
    float set_current_val;
    float set_rpm_val;
    float set_pos_val;
    uint32_t last_mask;
    int get_values_calls;
    vesc_stats_t stats;
    int stats_calls;
    int stats_resets;
} mock_comm_ctx_t;

/*
 * Identity facts are product input to the codec; the wire layout is what is under
 * test, so these are values nothing else depends on.
 */
static const uint8_t test_uuid[12] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u};
static const vesc_identity_t test_identity = {
    .hw_name = "TEST_HW",
    .fw_name = "test_fw",
    .uuid = test_uuid,
    .fw_version_major = 6u,
    .fw_version_minor = 2u,
    .pairing_done = 1u,
    .fw_test_version = 3u,
    .hw_type = 0u,
    .custom_cfg_num = 4u,
    .phase_filters = 1u,
    .qmlui_hw = 2u,
    .qmlui_app = 0u,
    .nrf_flags = 5u,
    .controller_id = 7u,
    .hw_crc = 0xDEADBEEFu,
};

/* Names long enough that the reply cannot be built without overflowing it. */
static const char oversized_name[80] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const vesc_identity_t oversized_identity = {
    .hw_name = oversized_name,
    .fw_name = oversized_name,
    .uuid = test_uuid,
};

static edge_status_t mock_stream_write(void *self, const uint8_t *data, size_t len) {
    mock_comm_ctx_t *ctx = (mock_comm_ctx_t *)self;
    if (len > sizeof(ctx->tx_buf)) {
        return EDGE_ENOSPC;
    }
    memcpy(ctx->tx_buf, data, len);
    ctx->tx_len = len;
    ctx->tx_count++;
    return EDGE_OK;
}

static edge_status_t mock_get_values(void *self, uint32_t mask, vesc_values_t *out_val) {
    mock_comm_ctx_t *ctx = (mock_comm_ctx_t *)self;
    ctx->last_mask = mask;
    ctx->get_values_calls++;
    *out_val = ctx->current_values;
    return EDGE_OK;
}

static edge_status_t mock_set_duty(void *self, float duty) {
    mock_comm_ctx_t *ctx = (mock_comm_ctx_t *)self;
    ctx->set_duty_val = duty;
    return EDGE_OK;
}

static edge_status_t mock_set_current(void *self, float current) {
    mock_comm_ctx_t *ctx = (mock_comm_ctx_t *)self;
    ctx->set_current_val = current;
    return EDGE_OK;
}

static edge_status_t mock_set_current_brake(void *self, float current) {
    mock_comm_ctx_t *ctx = (mock_comm_ctx_t *)self;
    ctx->set_current_val = -current;
    return EDGE_OK;
}

static edge_status_t mock_get_stats(void *self, vesc_stats_t *out_val) {
    mock_comm_ctx_t *ctx = (mock_comm_ctx_t *)self;
    ctx->stats_calls++;
    *out_val = ctx->stats;
    return EDGE_OK;
}

static edge_status_t mock_reset_stats(void *self) {
    mock_comm_ctx_t *ctx = (mock_comm_ctx_t *)self;
    ctx->stats_resets++;
    ctx->stats = (vesc_stats_t){0};
    return EDGE_OK;
}

static edge_status_t mock_set_rpm(void *self, float rpm) {
    mock_comm_ctx_t *ctx = (mock_comm_ctx_t *)self;
    ctx->set_rpm_val = rpm;
    return EDGE_OK;
}

static edge_status_t mock_set_pos(void *self, float pos) {
    mock_comm_ctx_t *ctx = (mock_comm_ctx_t *)self;
    ctx->set_pos_val = pos;
    return EDGE_OK;
}

static void test_crc16_calculation(void **state) {
    (void)state;
    const uint8_t test_data[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint16_t crc1 = vesc_crc16(test_data, sizeof(test_data));
    uint16_t crc2 = vesc_crc16(test_data, sizeof(test_data));
    assert_int_equal(crc1, crc2);
    assert_true(crc1 != 0);
}

static void test_send_packet_framing(void **state) {
    (void)state;
    mock_comm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    edge_stream_tx_port_t tx_port = {
        .write = mock_stream_write,
        .self = &ctx,
    };

    vesc_comm_t comm;
    vesc_comm_construct(&comm, EDGE_MOD_VESC_COMM, 10, &tx_port, NULL, &test_identity);
    assert_int_equal(vesc_comm_init(&comm), EDGE_OK);

    uint8_t payload[] = {0x04, 0x01, 0x02, 0x03};
    assert_int_equal(vesc_comm_send_packet(&comm, payload, sizeof(payload)), EDGE_OK);

    /* Framing: [2][len=4][payload: 4 bytes][crc: 2 bytes][3] = total 9 bytes */
    assert_int_equal(ctx.tx_len, 9);
    assert_int_equal(ctx.tx_buf[0], 2);
    assert_int_equal(ctx.tx_buf[1], 4);
    assert_int_equal(ctx.tx_buf[2], 0x04);
    assert_int_equal(ctx.tx_buf[3], 0x01);
    assert_int_equal(ctx.tx_buf[4], 0x02);
    assert_int_equal(ctx.tx_buf[5], 0x03);

    uint16_t expected_crc = vesc_crc16(payload, sizeof(payload));
    uint16_t actual_crc = ((uint16_t)ctx.tx_buf[6] << 8) | ctx.tx_buf[7];
    assert_int_equal(actual_crc, expected_crc);
    assert_int_equal(ctx.tx_buf[8], 3); /* Stop byte */
    assert_int_equal(comm.packets_sent, 1);
}

static void test_receive_packet_and_commands(void **state) {
    (void)state;
    mock_comm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    edge_stream_tx_port_t tx_port = {
        .write = mock_stream_write,
        .self = &ctx,
    };

    vesc_motor_provider_port_t motor_port = {
        .get_values = mock_get_values,
        .get_stats = mock_get_stats,
        .reset_stats = mock_reset_stats,
        .set_duty = mock_set_duty,
        .set_current = mock_set_current,
        .set_current_brake = mock_set_current_brake,
        .set_rpm = mock_set_rpm,
        .set_pos = mock_set_pos,
        .self = &ctx,
    };

    vesc_comm_t comm;
    vesc_comm_construct(&comm, EDGE_MOD_VESC_COMM, 10, &tx_port, &motor_port, &test_identity);
    assert_int_equal(vesc_comm_init(&comm), EDGE_OK);

    /* Test 1: COMM_SET_DUTY */
    /* payload: [COMM_SET_DUTY=5][duty*100000 = 0.5 * 100000 = 50000 = 0x0000C350] */
    uint8_t cmd_duty[] = {0x05, 0x00, 0x00, 0xC3, 0x50};
    uint16_t crc = vesc_crc16(cmd_duty, sizeof(cmd_duty));
    uint8_t frame[16];
    size_t f_idx = 0;
    frame[f_idx++] = 2;
    frame[f_idx++] = (uint8_t)sizeof(cmd_duty);
    memcpy(frame + f_idx, cmd_duty, sizeof(cmd_duty));
    f_idx += sizeof(cmd_duty);
    frame[f_idx++] = (uint8_t)(crc >> 8);
    frame[f_idx++] = (uint8_t)(crc & 0xFFu);
    frame[f_idx++] = 3;

    /* Feed byte-by-byte */
    for (size_t i = 0; i < f_idx; i++) {
        vesc_comm_process_byte(&comm, frame[i]);
    }

    assert_int_equal(comm.packets_received, 1);
    assert_true(fabsf(ctx.set_duty_val - 0.5f) < 1e-4f);

    /* Test 2: COMM_SET_RPM */
    /* payload: [COMM_SET_RPM=8][rpm = 3000 = 0x00000BB8] */
    uint8_t cmd_rpm[] = {0x08, 0x00, 0x00, 0x0B, 0xB8};
    crc = vesc_crc16(cmd_rpm, sizeof(cmd_rpm));
    f_idx = 0;
    frame[f_idx++] = 2;
    frame[f_idx++] = (uint8_t)sizeof(cmd_rpm);
    memcpy(frame + f_idx, cmd_rpm, sizeof(cmd_rpm));
    f_idx += sizeof(cmd_rpm);
    frame[f_idx++] = (uint8_t)(crc >> 8);
    frame[f_idx++] = (uint8_t)(crc & 0xFFu);
    frame[f_idx++] = 3;

    for (size_t i = 0; i < f_idx; i++) {
        vesc_comm_process_byte(&comm, frame[i]);
    }
    assert_int_equal(comm.packets_received, 2);
    assert_true(fabsf(ctx.set_rpm_val - 3000.0f) < 1e-4f);

    /* Test 3: COMM_FW_VERSION query */
    ctx.tx_count = 0;
    uint8_t cmd_fw[] = {0x00};
    crc = vesc_crc16(cmd_fw, sizeof(cmd_fw));
    f_idx = 0;
    frame[f_idx++] = 2;
    frame[f_idx++] = 1;
    frame[f_idx++] = cmd_fw[0];
    frame[f_idx++] = (uint8_t)(crc >> 8);
    frame[f_idx++] = (uint8_t)(crc & 0xFFu);
    frame[f_idx++] = 3;

    for (size_t i = 0; i < f_idx; i++) {
        vesc_comm_process_byte(&comm, frame[i]);
    }
    assert_int_equal(comm.packets_received, 3);
    assert_int_equal(ctx.tx_count, 1);

    /*
     * Byte-exact COMM_FW_VERSION, in the reference's field order:
     * id, major, minor, "HW_NAME\0", 12-byte uuid, pairing_done, test_version,
     * hw_type, custom_cfg_num, phase_filters, qmlui_hw, qmlui_app, nrf_flags,
     * "FW_NAME\0", uint32 hw_crc.  The nrf_flags byte sits between qmlui_app and
     * the firmware name; leaving it out shifts every later field by one.
     */
    const uint8_t *p = ctx.tx_buf + 2;
    assert_int_equal(p[0], COMM_FW_VERSION);
    assert_int_equal(p[1], 6u);
    assert_int_equal(p[2], 2u);
    assert_memory_equal(p + 3, "TEST_HW", 8u);

    const uint8_t uuid_expected[12] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u};
    assert_memory_equal(p + 11, uuid_expected, 12u);

    assert_int_equal(p[23], 1u); /* pairing_done */
    assert_int_equal(p[24], 3u); /* fw_test_version */
    assert_int_equal(p[25], 0u); /* hw_type */
    assert_int_equal(p[26], 4u); /* custom_cfg_num */
    assert_int_equal(p[27], 1u); /* phase_filters */
    assert_int_equal(p[28], 2u); /* qmlui_hw */
    assert_int_equal(p[29], 0u); /* qmlui_app */
    assert_int_equal(p[30], 5u); /* nrf_flags */

    assert_memory_equal(p + 31, "test_fw", 8u);
    assert_int_equal(p[39], 0xDEu);
    assert_int_equal(p[40], 0xADu);
    assert_int_equal(p[41], 0xBEu);
    assert_int_equal(p[42], 0xEFu);
    assert_int_equal(ctx.tx_buf[1], 43u); /* payload length byte */

    /* Identity that cannot fit is refused rather than truncated or overflowed. */
    vesc_comm_t oversize_comm;
    vesc_comm_construct(&oversize_comm, EDGE_MOD_VESC_COMM, 10, &tx_port, NULL,
                        &oversized_identity);
    assert_int_equal(vesc_comm_init(&oversize_comm), EDGE_OK);
    ctx.tx_count = 0;
    assert_int_equal(vesc_comm_process_command(&oversize_comm, cmd_fw, sizeof(cmd_fw)),
                     EDGE_EINVAL);
    assert_int_equal(ctx.tx_count, 0);

    /* A codec with no identity at all refuses the same way. */
    vesc_comm_construct(&oversize_comm, EDGE_MOD_VESC_COMM, 10, &tx_port, NULL, NULL);
    assert_int_equal(vesc_comm_init(&oversize_comm), EDGE_OK);
    assert_int_equal(vesc_comm_process_command(&oversize_comm, cmd_fw, sizeof(cmd_fw)),
                     EDGE_EINVAL);
    assert_int_equal(ctx.tx_count, 0);

    /*
     * Test 4: COMM_GET_VALUES_SELECTIVE. The mask decides both the payload and what
     * the provider is asked for, and it is echoed back before the fields: id, then
     * the 4-byte mask, then only the selected fields. A mask of "rpm only" must
     * therefore produce a 9-byte payload, not the 60-odd of a full reply.
     */
    ctx.tx_count = 0;
    ctx.get_values_calls = 0;
    ctx.last_mask = 0u;
    uint8_t cmd_sel[5] = {COMM_GET_VALUES_SELECTIVE, 0x00u, 0x00u, 0x00u, 0x80u}; /* 1 << 7 */
    crc = vesc_crc16(cmd_sel, sizeof(cmd_sel));
    f_idx = 0;
    frame[f_idx++] = 2; /* 8-bit length frame; a 16-bit frame must carry >= 255 bytes */
    frame[f_idx++] = 0x05u;
    for (size_t i = 0; i < sizeof(cmd_sel); i++) {
        frame[f_idx++] = cmd_sel[i];
    }
    frame[f_idx++] = (uint8_t)(crc >> 8);
    frame[f_idx++] = (uint8_t)(crc & 0xFFu);
    frame[f_idx++] = 3;

    for (size_t i = 0; i < f_idx; i++) {
        vesc_comm_process_byte(&comm, frame[i]);
    }

    assert_int_equal(ctx.tx_count, 1);
    assert_int_equal(ctx.get_values_calls, 1);
    assert_int_equal(ctx.last_mask, 1u << 7); /* the peer's mask reaches the provider */
    assert_int_equal(ctx.tx_buf[1], 9u);      /* mask(4) + rpm(4) + id(1) */
    const uint8_t *sel_p = ctx.tx_buf + 2;
    assert_int_equal(sel_p[0], COMM_GET_VALUES_SELECTIVE);
    assert_int_equal(sel_p[1], 0x00u); /* mask echoed, big endian */
    assert_int_equal(sel_p[2], 0x00u);
    assert_int_equal(sel_p[3], 0x00u);
    assert_int_equal(sel_p[4], 0x80u);
    /* rpm is the only field, float32 with scale 1e0, big endian */
    int32_t rpm_raw = (int32_t)(((uint32_t)sel_p[5] << 24) | ((uint32_t)sel_p[6] << 16) |
                                ((uint32_t)sel_p[7] << 8) | (uint32_t)sel_p[8]);
    assert_int_equal(rpm_raw, (int32_t)ctx.current_values.rpm);

    /* A SELECTIVE frame without its 4-byte mask is malformed, not a guess. */
    ctx.tx_count = 0;
    uint8_t cmd_short[1] = {COMM_GET_VALUES_SELECTIVE};
    assert_int_equal(vesc_comm_process_command(&comm, cmd_short, sizeof(cmd_short)), EDGE_EINVAL);
    assert_int_equal(ctx.tx_count, 0);

    /*
     * Test 4b: COMM_GET_STATS. The request mask is 16 bits and the reply echoes it
     * as 32; the fields go out as float32_auto, which is the IEEE-754 bit pattern
     * (the reference's encoding), so the expected bytes are readable at a glance.
     * The stats themselves are mock input - the wire contract is what is pinned.
     */
    ctx.tx_count = 0;
    ctx.stats_calls = 0;
    ctx.stats_resets = 0;
    ctx.stats = (vesc_stats_t){0};
    ctx.stats.power_avg = 24.0f;  /* 0x41C00000 */
    ctx.stats.current_avg = 8.0f; /* 0x41000000 */

    uint8_t cmd_stats[3] = {COMM_GET_STATS, 0x00u, 0x30u}; /* bits 4 and 5 */
    crc = vesc_crc16(cmd_stats, sizeof(cmd_stats));
    f_idx = 0;
    frame[f_idx++] = 2;
    frame[f_idx++] = (uint8_t)sizeof(cmd_stats);
    for (size_t i = 0; i < sizeof(cmd_stats); i++) {
        frame[f_idx++] = cmd_stats[i];
    }
    frame[f_idx++] = (uint8_t)(crc >> 8);
    frame[f_idx++] = (uint8_t)(crc & 0xFFu);
    frame[f_idx++] = 3;

    for (size_t i = 0; i < f_idx; i++) {
        vesc_comm_process_byte(&comm, frame[i]);
    }

    assert_int_equal(ctx.tx_count, 1);
    assert_int_equal(ctx.stats_calls, 1);
    /* id(1) + mask(4) + current_avg(4) + current_max(4) = 13 bytes */
    assert_int_equal(ctx.tx_buf[1], 13u);
    const uint8_t *sp = ctx.tx_buf + 2;
    assert_int_equal(sp[0], COMM_GET_STATS);
    assert_int_equal(sp[1], 0x00u); /* the uint16 request mask, widened to 32 */
    assert_int_equal(sp[2], 0x00u);
    assert_int_equal(sp[3], 0x00u);
    assert_int_equal(sp[4], 0x30u);
    assert_int_equal(sp[5], 0x41u); /* current_avg = 8.0f */
    assert_int_equal(sp[6], 0x00u);
    assert_int_equal(sp[7], 0x00u);
    assert_int_equal(sp[8], 0x00u);
    for (int i = 9; i < 13; i++) {
        assert_int_equal(sp[i], 0x00u); /* current_max, left at 0 by the mock */
    }

    /* COMM_RESET_STATS replies only when its ack byte is set. */
    uint8_t cmd_reset_quiet[1] = {COMM_RESET_STATS};
    ctx.tx_count = 0;
    assert_int_equal(vesc_comm_process_command(&comm, cmd_reset_quiet, sizeof(cmd_reset_quiet)),
                     EDGE_OK);
    assert_int_equal(ctx.stats_resets, 1);
    assert_int_equal(ctx.tx_count, 0);

    uint8_t cmd_reset_ack[2] = {COMM_RESET_STATS, 0x01u};
    ctx.tx_count = 0;
    assert_int_equal(vesc_comm_process_command(&comm, cmd_reset_ack, sizeof(cmd_reset_ack)),
                     EDGE_OK);
    assert_int_equal(ctx.stats_resets, 2);
    assert_int_equal(ctx.tx_count, 1);
    assert_int_equal(ctx.tx_buf[1], 1u); /* id only */
    assert_int_equal(ctx.tx_buf[2], COMM_RESET_STATS);

    /* Test 4: Corrupted CRC */
    frame[f_idx - 2] ^= 0xFF; /* Corrupt CRC */
    for (size_t i = 0; i < f_idx; i++) {
        vesc_comm_process_byte(&comm, frame[i]);
    }
    assert_int_equal(comm.crc_errors, 1);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_crc16_calculation),
        cmocka_unit_test(test_send_packet_framing),
        cmocka_unit_test(test_receive_packet_and_commands),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
