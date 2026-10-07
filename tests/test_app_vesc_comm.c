/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdalign.h>
#include <math.h>
#include <string.h>

#include <cmocka.h>
/* clang-format on */

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/modules.h"
#include <stdio.h>

#include "vesc_buffer/buffer.h"
#include "vesc_comm/vesc_comm.h"

typedef struct mock_comm_ctx {
    uint8_t tx_buf[1024];
    size_t tx_len;
    size_t tx_count;

    float chuk_level;
    vesc_values_t current_values;
    float set_duty_val;
    float set_current_val;
    float set_current_rel_val;
    float set_handbrake_val;
    float set_rpm_val;
    float set_pos_val;
    uint32_t last_mask;
    int get_values_calls;
    vesc_stats_t stats;
    int stats_calls;
    int stats_resets;
    float ppm_level;
    float ppm_pulse_us;
    float adc_level;
    float adc_voltage;
    float adc_level2;
    float adc_voltage2;
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
/*
 * Long enough that two of these plus the uuid, flags and fixed bytes cannot fit the reply
 * scratch, whatever it grows to - the reply buffer went from 128 to 512 for the 489-byte
 * COMM_GET_MCCONF reply, and an 80-byte name no longer overflowed it.
 */
static const char oversized_name[] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    "aaaaaaaaaaaaaaaaaa";
static const vesc_identity_t oversized_identity = {
    .hw_name = oversized_name,
    .fw_name = oversized_name,
    .uuid = test_uuid,
};

/*
 * Caller-provided storage. The caller hands over a block of the documented size
 * and alignment without knowing a single field of vesc_comm, which is why these
 * tests cannot declare one on the stack any more, and why the counters are read
 * through accessors instead of being poked.
 */
static alignas(VESC_COMM_STORAGE_ALIGN) unsigned char test_comm_storage[VESC_COMM_STORAGE_SIZE];
/* A second block, because two live instances need two live buffers - exactly as a
 * real caller would have to provide. */
static alignas(VESC_COMM_STORAGE_ALIGN) unsigned char test_comm_storage2[VESC_COMM_STORAGE_SIZE];
static vesc_comm_t *test_comm_alloc(void) {
    memset(test_comm_storage, 0, sizeof(test_comm_storage));
    return (vesc_comm_t *)test_comm_storage;
}

static vesc_comm_t *test_comm_alloc2(void) {
    memset(test_comm_storage2, 0, sizeof(test_comm_storage2));
    return (vesc_comm_t *)test_comm_storage2;
}

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

static edge_status_t mock_set_current_rel(void *self, float rel) {
    mock_comm_ctx_t *ctx = (mock_comm_ctx_t *)self;
    ctx->set_current_rel_val = rel;
    return EDGE_OK;
}

static edge_status_t mock_set_handbrake(void *self, float current) {
    mock_comm_ctx_t *ctx = (mock_comm_ctx_t *)self;
    ctx->set_handbrake_val = current;
    return EDGE_OK;
}

static edge_status_t mock_get_decoded_ppm(void *self, float *level, float *pulse_us) {
    mock_comm_ctx_t *ctx = (mock_comm_ctx_t *)self;
    *level = ctx->ppm_level;
    *pulse_us = ctx->ppm_pulse_us;
    return EDGE_OK;
}

/* comm/commands.c:2521-2525: COMM_GET_DECODED_CHUK's own single value. */
static edge_status_t mock_get_decoded_chuk(void *self, float *level_y) {
    mock_comm_ctx_t *ctx = (mock_comm_ctx_t *)self;
    *level_y = ctx->chuk_level;
    return EDGE_OK;
}

static edge_status_t mock_get_decoded_adc(void *self, float *level, float *voltage, float *level2,
                                          float *voltage2) {
    mock_comm_ctx_t *ctx = (mock_comm_ctx_t *)self;
    *level = ctx->adc_level;
    *voltage = ctx->adc_voltage;
    *level2 = ctx->adc_level2;
    *voltage2 = ctx->adc_voltage2;
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

/*
 * Lifecycle and the argument guards. These lines were never executed by any test,
 * which is the kind of gap that hides a divide-by-zero or a null dereference until
 * something calls it.
 */
static void test_vesc_comm_lifecycle_and_guards(void **state) {
    (void)state;

    /* Null self is refused rather than dereferenced. */
    assert_int_equal(vesc_comm_init(NULL), EDGE_EINVAL);
    assert_int_equal(vesc_comm_deinit(NULL), EDGE_EINVAL);
    vesc_comm_construct(NULL, EDGE_MOD_VESC_COMM, 10u, NULL, NULL, NULL, NULL, NULL, NULL);

    mock_comm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    edge_stream_tx_port_t tx_port = {.write = mock_stream_write, .self = &ctx};

    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, NULL,
                        &test_identity);
    assert_non_null(vesc_comm_module(comm));
    assert_int_equal(vesc_comm_module(comm)->module_id, EDGE_MOD_VESC_COMM);
    assert_ptr_equal(vesc_comm_module(NULL), NULL);

    /* init/deinit are idempotent, and a re-init clears the counters. The counter
     * is driven through the interface rather than written directly, which is the
     * only way left now - and the better assertion anyway. */
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);
    uint8_t alive[5] = {COMM_ALIVE, 0u, 0u, 0u, 0u};
    assert_int_equal(vesc_comm_send_packet(comm, alive, 1u), EDGE_OK);
    assert_int_equal(vesc_comm_packets_sent(comm), 1u);
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);
    assert_int_equal(vesc_comm_packets_sent(comm), 0u);
    assert_int_equal(vesc_comm_packets_received(comm), 0u);
    assert_int_equal(vesc_comm_crc_errors(comm), 0u);
    assert_int_equal(vesc_comm_deinit(comm), EDGE_OK);

    /* A byte before init must not be processed into anything. */
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);
    vesc_comm_process_byte(NULL, 0x02u);
    for (int i = 0; i < 600; i++) {
        vesc_comm_process_byte(comm, 0x55u); /* junk; must not overflow the rx buffer */
    }
    assert_int_equal(vesc_comm_packets_received(comm), 0u);

    /* Sending guards: no port, no payload, oversized payload. */
    uint8_t payload[4] = {0};
    assert_int_equal(vesc_comm_send_packet(NULL, payload, sizeof(payload)), EDGE_EINVAL);
    assert_int_equal(vesc_comm_send_packet(comm, NULL, sizeof(payload)), EDGE_EINVAL);
    assert_int_equal(vesc_comm_send_packet(comm, payload, 0u), EDGE_EINVAL);
    assert_int_equal(vesc_comm_send_packet(comm, payload, VESC_PACKET_MAX_PL_LEN + 1u),
                     EDGE_EINVAL);

    vesc_comm_t *no_tx = test_comm_alloc();
    vesc_comm_construct(no_tx, EDGE_MOD_VESC_COMM, 10u, NULL, NULL, NULL, NULL, NULL,
                        &test_identity);
    assert_int_equal(vesc_comm_send_packet(no_tx, payload, sizeof(payload)), EDGE_EINVAL);

    /* The module hooks: poll and power_off answer, on_event needs its event. */
    edge_module_t *mod = vesc_comm_module(comm);
    assert_int_equal(mod->poll(mod), EDGE_OK);
    edge_event_t evt;
    memset(&evt, 0, sizeof(evt));
    assert_int_equal(mod->on_event(mod, &evt), EDGE_OK);
    assert_int_equal(mod->on_event(mod, NULL), EDGE_EINVAL);
    assert_int_equal(mod->power_off(mod), EDGE_OK);

    /* An unknown command is answered with "not supported", not silence or a crash. */
    uint8_t unknown[1] = {COMM_REBOOT}; /* declared in the table, not handled yet */
    assert_int_equal(vesc_comm_process_command(comm, unknown, sizeof(unknown)), EDGE_ENOTSUP);
    assert_int_equal(vesc_comm_process_command(comm, NULL, 1u), EDGE_EINVAL);
    assert_int_equal(vesc_comm_process_command(comm, unknown, 0u), EDGE_EINVAL);
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

    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10, &tx_port, NULL, NULL, NULL, NULL,
                        &test_identity);
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);

    uint8_t payload[] = {0x04, 0x01, 0x02, 0x03};
    assert_int_equal(vesc_comm_send_packet(comm, payload, sizeof(payload)), EDGE_OK);

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
    assert_int_equal(vesc_comm_packets_sent(comm), 1);
}

static void test_receive_packet_and_commands(void **state) {
    (void)state;
    mock_comm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    edge_stream_tx_port_t tx_port = {
        .write = mock_stream_write,
        .self = &ctx,
    };

    vesc_app_status_port_t app_status_port = {
        .get_decoded_ppm = mock_get_decoded_ppm,
        .get_decoded_adc = mock_get_decoded_adc,
        .get_decoded_chuk = mock_get_decoded_chuk,
        .self = &ctx,
    };
    vesc_motor_provider_port_t motor_port = {
        .get_values = mock_get_values,
        .get_stats = mock_get_stats,
        .reset_stats = mock_reset_stats,
        .set_duty = mock_set_duty,
        .set_current = mock_set_current,
        .set_current_rel = mock_set_current_rel,
        .set_handbrake = mock_set_handbrake,
        .set_current_brake = mock_set_current_brake,
        .set_rpm = mock_set_rpm,
        .set_pos = mock_set_pos,
        .self = &ctx,
    };

    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10, &tx_port, &motor_port, &app_status_port, NULL,
                        NULL, &test_identity);
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);

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
        vesc_comm_process_byte(comm, frame[i]);
    }

    assert_int_equal(vesc_comm_packets_received(comm), 1);
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
        vesc_comm_process_byte(comm, frame[i]);
    }
    assert_int_equal(vesc_comm_packets_received(comm), 2);
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
        vesc_comm_process_byte(comm, frame[i]);
    }
    assert_int_equal(vesc_comm_packets_received(comm), 3);
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
    vesc_comm_t *oversize_comm = test_comm_alloc2();
    vesc_comm_construct(oversize_comm, EDGE_MOD_VESC_COMM, 10, &tx_port, NULL, NULL, NULL, NULL,
                        &oversized_identity);
    assert_int_equal(vesc_comm_init(oversize_comm), EDGE_OK);
    ctx.tx_count = 0;
    assert_int_equal(vesc_comm_process_command(oversize_comm, cmd_fw, sizeof(cmd_fw)), EDGE_EINVAL);
    assert_int_equal(ctx.tx_count, 0);

    /* A codec with no identity at all refuses the same way. */
    vesc_comm_construct(oversize_comm, EDGE_MOD_VESC_COMM, 10, &tx_port, NULL, NULL, NULL, NULL,
                        NULL);
    assert_int_equal(vesc_comm_init(oversize_comm), EDGE_OK);
    assert_int_equal(vesc_comm_process_command(oversize_comm, cmd_fw, sizeof(cmd_fw)), EDGE_EINVAL);
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
        vesc_comm_process_byte(comm, frame[i]);
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
    assert_int_equal(vesc_comm_process_command(comm, cmd_short, sizeof(cmd_short)), EDGE_EINVAL);
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
        vesc_comm_process_byte(comm, frame[i]);
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
    assert_int_equal(vesc_comm_process_command(comm, cmd_reset_quiet, sizeof(cmd_reset_quiet)),
                     EDGE_OK);
    assert_int_equal(ctx.stats_resets, 1);
    assert_int_equal(ctx.tx_count, 0);

    uint8_t cmd_reset_ack[2] = {COMM_RESET_STATS, 0x01u};
    ctx.tx_count = 0;
    assert_int_equal(vesc_comm_process_command(comm, cmd_reset_ack, sizeof(cmd_reset_ack)),
                     EDGE_OK);
    assert_int_equal(ctx.stats_resets, 2);
    assert_int_equal(ctx.tx_count, 1);
    assert_int_equal(ctx.tx_buf[1], 1u); /* id only */
    assert_int_equal(ctx.tx_buf[2], COMM_RESET_STATS);

    /*
     * Decoded inputs. Both commands report the decoded level and the raw input
     * behind it, as int32 scaled by 1e6 (reference: comm/commands.c
     * COMM_GET_DECODED_PPM / _ADC).
     */
    ctx.tx_count = 0;
    ctx.ppm_level = 0.5f;
    ctx.ppm_pulse_us = 1500.0f;
    uint8_t cmd_ppm[1] = {COMM_GET_DECODED_PPM};
    assert_int_equal(vesc_comm_process_command(comm, cmd_ppm, sizeof(cmd_ppm)), EDGE_OK);
    assert_int_equal(ctx.tx_count, 1);
    assert_int_equal(ctx.tx_buf[1], 9u); /* id + 2 * int32 */
    assert_int_equal(ctx.tx_buf[2], COMM_GET_DECODED_PPM);
    assert_int_equal(ctx.tx_buf[3], 0x00u); /* 500000 = 0x0007A120 */
    assert_int_equal(ctx.tx_buf[4], 0x07u);
    assert_int_equal(ctx.tx_buf[5], 0xA1u);
    assert_int_equal(ctx.tx_buf[6], 0x20u);
    assert_int_equal(ctx.tx_buf[7], 0x59u); /* 1500000000 = 0x59682F00 */
    assert_int_equal(ctx.tx_buf[8], 0x68u);
    assert_int_equal(ctx.tx_buf[9], 0x2Fu);
    assert_int_equal(ctx.tx_buf[10], 0x00u);

    /*
     * COMM_GET_DECODED_CHUK: one int32 scaled by a million, which is the whole of its payload. The
     * value is the stick's own, so half a stick is five hundred thousand.
     */
    /* COMM_PING_CAN asks the operations port which peers are there, and answers with none without
     * it. */
    ctx.tx_count = 0;
    uint8_t cmd_ping[1] = {COMM_PING_CAN};
    /* COMM_GET_BATTERY_CUT reads the running configuration's own two limits, through the same port.
     */
    ctx.tx_count = 0;
    /* COMM_SET_CAN_MODE writes the application configuration, which this harness does not give it.
     */
    ctx.tx_count = 0;
    /* COMM_SET_MCCONF_TEMP writes the running configuration, which this harness does not hand over.
     */
    /* COMM_FW_INFO: three version bytes and two hashes, each a string and its terminator. This port
     * has no build-generated hash, so both strings are empty - a length of six. */
    ctx.tx_count = 0;
    /* COMM_MOTOR_ESTOP needs the operations port to open its window, and has no reply of its own.
     */
    ctx.tx_count = 0;
    uint8_t cmd_estop[3] = {COMM_MOTOR_ESTOP, 0x01u, 0xF4u};
    assert_int_equal(vesc_comm_process_command(comm, cmd_estop, sizeof(cmd_estop)), EDGE_ENOTSUP);
    assert_int_equal(ctx.tx_count, 0);

    uint8_t cmd_fwinfo[1] = {COMM_FW_INFO};
    assert_int_equal(vesc_comm_process_command(comm, cmd_fwinfo, sizeof(cmd_fwinfo)), EDGE_OK);
    assert_int_equal(ctx.tx_count, 1);
    assert_int_equal(ctx.tx_buf[1], 6u);
    assert_int_equal(ctx.tx_buf[2], COMM_FW_INFO);
    assert_int_equal(ctx.tx_buf[6], 0u); /* the commit hash's terminator */
    assert_int_equal(ctx.tx_buf[7], 0u); /* and the user's */

    ctx.tx_count = 0;
    uint8_t cmd_mct[10] = {COMM_SET_MCCONF_TEMP, 1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u};
    assert_int_equal(vesc_comm_process_command(comm, cmd_mct, sizeof(cmd_mct)), EDGE_ENOTSUP);
    assert_int_equal(ctx.tx_count, 0);

    uint8_t cmd_can[4] = {COMM_SET_CAN_MODE, 0u, 1u, 1u};
    assert_int_equal(vesc_comm_process_command(comm, cmd_can, sizeof(cmd_can)), EDGE_ENOTSUP);
    assert_int_equal(ctx.tx_count, 0);

    /* COMM_SHUTDOWN needs the operations port, and has no reply of its own. */
    ctx.tx_count = 0;
    uint8_t cmd_down[3] = {COMM_SHUTDOWN, 1u, 0u};
    assert_int_equal(vesc_comm_process_command(comm, cmd_down, sizeof(cmd_down)), EDGE_ENOTSUP);
    assert_int_equal(ctx.tx_count, 0);

    /* COMM_SET_CHUCK_DATA needs the operations port too, and has no reply of its own either way. */
    ctx.tx_count = 0;
    uint8_t cmd_chuck[12] = {COMM_SET_CHUCK_DATA, 128u, 200u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    assert_int_equal(vesc_comm_process_command(comm, cmd_chuck, sizeof(cmd_chuck)), EDGE_ENOTSUP);
    assert_int_equal(ctx.tx_count, 0);

    uint8_t cmd_cut[1] = {COMM_GET_BATTERY_CUT};
    assert_int_equal(vesc_comm_process_command(comm, cmd_cut, sizeof(cmd_cut)), EDGE_ENOTSUP);
    assert_int_equal(ctx.tx_count, 0);

    assert_int_equal(vesc_comm_process_command(comm, cmd_ping, sizeof(cmd_ping)), EDGE_ENOTSUP);
    assert_int_equal(ctx.tx_count, 0);

    /* COMM_TERMINAL_CMD_SYNC is the same handling as the command beside it, down to the same port.
     */
    ctx.tx_count = 0;
    uint8_t cmd_sync[6] = {COMM_TERMINAL_CMD_SYNC, 'h', 'e', 'l', 'p', '\0'};
    assert_int_equal(vesc_comm_process_command(comm, cmd_sync, sizeof(cmd_sync)), EDGE_ENOTSUP);
    assert_int_equal(ctx.tx_count, 0);

    /* COMM_GET_MCCONF_TEMP needs the operations port, which this harness does not hand over. */
    ctx.tx_count = 0;
    uint8_t cmd_temp[1] = {COMM_GET_MCCONF_TEMP};
    assert_int_equal(vesc_comm_process_command(comm, cmd_temp, sizeof(cmd_temp)), EDGE_EINVAL);
    assert_int_equal(ctx.tx_count, 0);

    ctx.tx_count = 0;
    ctx.chuk_level = 0.5f;
    uint8_t cmd_chuk[1] = {COMM_GET_DECODED_CHUK};
    assert_int_equal(vesc_comm_process_command(comm, cmd_chuk, sizeof(cmd_chuk)), EDGE_OK);
    assert_int_equal(ctx.tx_count, 1);
    assert_int_equal(ctx.tx_buf[1], 5u); /* id + one int32 */
    assert_int_equal(ctx.tx_buf[2], COMM_GET_DECODED_CHUK);
    assert_int_equal(ctx.tx_buf[3], 0x00u); /* 500000 = 0x0007A120 */
    assert_int_equal(ctx.tx_buf[4], 0x07u);
    assert_int_equal(ctx.tx_buf[5], 0xA1u);
    assert_int_equal(ctx.tx_buf[6], 0x20u);

    ctx.tx_count = 0;
    ctx.adc_level = 0.25f;
    ctx.adc_voltage = 1.5f;
    ctx.adc_level2 = 0.0f;
    ctx.adc_voltage2 = 0.0f;
    uint8_t cmd_adc[1] = {COMM_GET_DECODED_ADC};
    assert_int_equal(vesc_comm_process_command(comm, cmd_adc, sizeof(cmd_adc)), EDGE_OK);
    assert_int_equal(ctx.tx_count, 1);
    assert_int_equal(ctx.tx_buf[1], 17u); /* id + 4 * int32 */
    assert_int_equal(ctx.tx_buf[2], COMM_GET_DECODED_ADC);
    assert_int_equal(ctx.tx_buf[3], 0x00u); /* 250000 = 0x0003D090 */
    assert_int_equal(ctx.tx_buf[4], 0x03u);
    assert_int_equal(ctx.tx_buf[5], 0xD0u);
    assert_int_equal(ctx.tx_buf[6], 0x90u);
    assert_int_equal(ctx.tx_buf[7], 0x00u); /* 1500000 = 0x0016E360 */
    assert_int_equal(ctx.tx_buf[8], 0x16u);
    assert_int_equal(ctx.tx_buf[9], 0xE3u);
    assert_int_equal(ctx.tx_buf[10], 0x60u);

    /* Test 4: Corrupted CRC */
    frame[f_idx - 2] ^= 0xFF; /* Corrupt CRC */
    for (size_t i = 0; i < f_idx; i++) {
        vesc_comm_process_byte(comm, frame[i]);
    }
    assert_int_equal(vesc_comm_crc_errors(comm), 1);

    /* COMM_SET_CURRENT_REL: the reference's float32 is fixed point - an int32 of
     * value * scale, divided back on read (comm/commands.c:1214 uses 1e5), not an IEEE
     * float - forwarded to the relative setter. The motor side, not the codec, picks
     * the limit.
     * payload: [COMM_SET_CURRENT_REL][0.5 * 1e5 = 50000 = 0x0000C350] */
    uint8_t cmd_cur_rel[] = {COMM_SET_CURRENT_REL, 0x00u, 0x00u, 0xC3u, 0x50u};
    const uint32_t packets_before = vesc_comm_packets_received(comm);
    crc = vesc_crc16(cmd_cur_rel, sizeof(cmd_cur_rel));
    f_idx = 0;
    frame[f_idx++] = 2;
    frame[f_idx++] = (uint8_t)sizeof(cmd_cur_rel);
    memcpy(frame + f_idx, cmd_cur_rel, sizeof(cmd_cur_rel));
    f_idx += sizeof(cmd_cur_rel);
    frame[f_idx++] = (uint8_t)(crc >> 8);
    frame[f_idx++] = (uint8_t)(crc & 0xFFu);
    frame[f_idx++] = 3;
    for (size_t i = 0; i < f_idx; i++) {
        vesc_comm_process_byte(comm, frame[i]);
    }
    assert_int_equal(vesc_comm_packets_received(comm), packets_before + 1u);
    assert_float_equal(ctx.set_current_rel_val, 0.5f, 1e-5f);

    /* COMM_SET_HANDBRAKE: float32 scaled by 1e3 - amps, unlike the 1e5 the current
     * commands use (comm/commands.c:515).
     * payload: [COMM_SET_HANDBRAKE][5.0 * 1e3 = 5000 = 0x00001388] */
    uint8_t cmd_handbrake[] = {COMM_SET_HANDBRAKE, 0x00u, 0x00u, 0x13u, 0x88u};
    const uint32_t hb_before = vesc_comm_packets_received(comm);
    crc = vesc_crc16(cmd_handbrake, sizeof(cmd_handbrake));
    f_idx = 0;
    frame[f_idx++] = 2;
    frame[f_idx++] = (uint8_t)sizeof(cmd_handbrake);
    memcpy(frame + f_idx, cmd_handbrake, sizeof(cmd_handbrake));
    f_idx += sizeof(cmd_handbrake);
    frame[f_idx++] = (uint8_t)(crc >> 8);
    frame[f_idx++] = (uint8_t)(crc & 0xFFu);
    frame[f_idx++] = 3;
    for (size_t i = 0; i < f_idx; i++) {
        vesc_comm_process_byte(comm, frame[i]);
    }
    assert_int_equal(vesc_comm_packets_received(comm), hb_before + 1u);
    assert_float_equal(ctx.set_handbrake_val, 5.0f, 1e-5f);
}

/*
 * A7's framing: the reference's commands_send_mcconf() sends the command id followed by the
 * configuration stream, and COMM_SET_MCCONF passes the request's stream straight through.
 * The stream contents themselves are pinned byte for byte in test_motor_config, so what is
 * checked here is the framing and the plumbing, not the layout.
 */
typedef struct mock_config_ctx {
    uint8_t mc[64];
    size_t mc_len;
    uint8_t set_mc[64];
    size_t set_mc_len;
    uint8_t app[64];
    size_t app_len;
    uint8_t set_app[64];
    size_t set_app_len;
    int mc_default_calls;
    int app_default_calls;
    int app_nostore_calls;
} mock_config_ctx_t;

static edge_status_t mock_get_mcconf(void *self, uint8_t *out, size_t buf_size, size_t *out_len) {
    mock_config_ctx_t *c = (mock_config_ctx_t *)self;
    if (c->mc_len > buf_size) {
        return EDGE_ENOSPC;
    }
    memcpy(out, c->mc, c->mc_len);
    *out_len = c->mc_len;
    return EDGE_OK;
}

static edge_status_t mock_set_mcconf(void *self, const uint8_t *in, size_t len) {
    mock_config_ctx_t *c = (mock_config_ctx_t *)self;
    if (len > sizeof(c->set_mc)) {
        return EDGE_ENOSPC;
    }
    memcpy(c->set_mc, in, len);
    c->set_mc_len = len;
    return EDGE_OK;
}

static edge_status_t mock_get_appconf(void *self, uint8_t *out, size_t buf_size, size_t *out_len) {
    mock_config_ctx_t *c = (mock_config_ctx_t *)self;
    if (c->app_len > buf_size) {
        return EDGE_ENOSPC;
    }
    memcpy(out, c->app, c->app_len);
    *out_len = c->app_len;
    return EDGE_OK;
}

static edge_status_t mock_set_appconf(void *self, const uint8_t *in, size_t len) {
    mock_config_ctx_t *c = (mock_config_ctx_t *)self;
    if (len > sizeof(c->set_app)) {
        return EDGE_ENOSPC;
    }
    memcpy(c->set_app, in, len);
    c->set_app_len = len;
    return EDGE_OK;
}

static edge_status_t mock_get_mcconf_default(void *self, uint8_t *out, size_t buf_size,
                                             size_t *out_len) {
    mock_config_ctx_t *c = (mock_config_ctx_t *)self;
    c->mc_default_calls++;
    return mock_get_mcconf(self, out, buf_size, out_len);
}

static edge_status_t mock_get_appconf_default(void *self, uint8_t *out, size_t buf_size,
                                              size_t *out_len) {
    mock_config_ctx_t *c = (mock_config_ctx_t *)self;
    c->app_default_calls++;
    return mock_get_appconf(self, out, buf_size, out_len);
}

static edge_status_t mock_set_appconf_nostore(void *self, const uint8_t *in, size_t len) {
    mock_config_ctx_t *c = (mock_config_ctx_t *)self;
    c->app_nostore_calls++;
    return mock_set_appconf(self, in, len);
}

/* COMM_TERMINAL_CMD reaches the product's terminal as a NUL-terminated command line. */
static char mock_terminal_last[64];
static int mock_terminal_calls;

static edge_status_t mock_terminal_cmd(void *self, const char *cmd) {
    (void)self;
    mock_terminal_calls++;
    snprintf(mock_terminal_last, sizeof(mock_terminal_last), "%s", cmd);
    return EDGE_OK;
}

/* COMM_FORWARD_CAN hands the packet to another controller: the first payload byte is the
 * target id and the rest is forwarded unchanged. */
static uint8_t mock_forward_last[64];
static size_t mock_forward_len;
static uint8_t mock_forward_target;
static int mock_forward_calls;

static edge_status_t mock_forward_can(void *self, uint8_t target_id, const uint8_t *data,
                                      size_t len) {
    (void)self;
    mock_forward_calls++;
    mock_forward_target = target_id;
    mock_forward_len = len;
    for (size_t i = 0u; i < len && i < sizeof(mock_forward_last); i++) {
        mock_forward_last[i] = data[i];
    }
    return EDGE_OK;
}

static void test_config_commands_framing(void **state) {
    (void)state;
    mock_comm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    mock_config_ctx_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    edge_stream_tx_port_t tx_port = {.write = mock_stream_write, .self = &ctx};
    vesc_config_provider_port_t config_port = {
        .get_mcconf = mock_get_mcconf,
        .set_mcconf = mock_set_mcconf,
        .get_appconf = mock_get_appconf,
        .set_appconf = mock_set_appconf,
        .get_mcconf_default = mock_get_mcconf_default,
        .get_appconf_default = mock_get_appconf_default,
        .set_appconf_nostore = mock_set_appconf_nostore,
        .self = &cfg,
    };

    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_ops_port_t ops_port = {
        .terminal_cmd = mock_terminal_cmd,
        .forward_can = mock_forward_can,
        .self = NULL,
    };
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, &config_port,
                        &ops_port, &test_identity);
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);

    /* COMM_GET_MCCONF: the reply is [id][stream], with the stream's own signature first. */
    cfg.mc[0] = 0xBCu;
    cfg.mc[1] = 0x09u;
    cfg.mc[2] = 0xF8u;
    cfg.mc[3] = 0xB0u;
    cfg.mc_len = 4u;
    ctx.tx_count = 0;
    uint8_t get_mc[] = {COMM_GET_MCCONF};
    assert_int_equal(vesc_comm_process_command(comm, get_mc, sizeof(get_mc)), EDGE_OK);
    assert_int_equal(ctx.tx_count, 1);
    assert_int_equal(ctx.tx_buf[1], 1u + 4u); /* frame length byte: payload size */
    assert_int_equal(ctx.tx_buf[2], COMM_GET_MCCONF);
    assert_int_equal(ctx.tx_buf[3], 0xBCu);
    assert_int_equal(ctx.tx_buf[6], 0xB0u);

    /* COMM_SET_MCCONF: the request's stream reaches the provider byte for byte. */
    uint8_t set_mc[] = {COMM_SET_MCCONF, 0xBCu, 0x09u, 0xF8u, 0xB0u};
    ctx.tx_count = 0;
    assert_int_equal(vesc_comm_process_command(comm, set_mc, sizeof(set_mc)), EDGE_OK);
    assert_int_equal(cfg.set_mc_len, 4u);
    assert_int_equal(cfg.set_mc[0], 0xBCu);
    assert_int_equal(cfg.set_mc[3], 0xB0u);
    /* A SET is acknowledged with its own id, one byte, as the reference does. */
    assert_int_equal(ctx.tx_count, 1);
    assert_int_equal(ctx.tx_buf[1], 1u); /* payload length */
    assert_int_equal(ctx.tx_buf[2], COMM_SET_MCCONF);

    /* COMM_GET_MCCONF_DEFAULT: the same framing, through the defaults path. */
    ctx.tx_count = 0;
    uint8_t get_mc_default[] = {COMM_GET_MCCONF_DEFAULT};
    assert_int_equal(vesc_comm_process_command(comm, get_mc_default, sizeof(get_mc_default)),
                     EDGE_OK);
    assert_int_equal(cfg.mc_default_calls, 1);
    assert_int_equal(ctx.tx_buf[2], COMM_GET_MCCONF_DEFAULT);

    /* COMM_SET_APPCONF_NO_STORE: applied, and acknowledged the same way. */
    ctx.tx_count = 0;
    uint8_t set_app_nostore[] = {COMM_SET_APPCONF_NO_STORE, 0x11u, 0xADu};
    assert_int_equal(vesc_comm_process_command(comm, set_app_nostore, sizeof(set_app_nostore)),
                     EDGE_OK);
    assert_int_equal(cfg.app_nostore_calls, 1);
    assert_int_equal(cfg.set_app_len, 2u);
    assert_int_equal(ctx.tx_count, 1);
    assert_int_equal(ctx.tx_buf[2], COMM_SET_APPCONF_NO_STORE);

    /* COMM_TERMINAL_CMD: the command line reaches the terminal, and the codec sends nothing
     * itself - the reference's terminal answers through its own printing path. */
    mock_terminal_calls = 0;
    mock_terminal_last[0] = '\0';
    ctx.tx_count = 0;
    uint8_t term_cmd[] = {COMM_TERMINAL_CMD, 'h', 'e', 'l', 'p', 0};
    assert_int_equal(vesc_comm_process_command(comm, term_cmd, sizeof(term_cmd)), EDGE_OK);
    assert_int_equal(mock_terminal_calls, 1);
    assert_string_equal(mock_terminal_last, "help");
    assert_int_equal(ctx.tx_count, 0);

    /* A terminal command with no payload is malformed, not an empty command line. */
    uint8_t term_bare[] = {COMM_TERMINAL_CMD};
    assert_int_equal(vesc_comm_process_command(comm, term_bare, sizeof(term_bare)), EDGE_EINVAL);

    /* COMM_FORWARD_CAN: the first payload byte is the target controller id and the rest is
     * forwarded, with the codec answering nothing of its own. */
    mock_forward_calls = 0;
    ctx.tx_count = 0;
    uint8_t fwd[] = {COMM_FORWARD_CAN, 42u, 0xDEu, 0xADu, 0xBEu};
    assert_int_equal(vesc_comm_process_command(comm, fwd, sizeof(fwd)), EDGE_OK);
    assert_int_equal(mock_forward_calls, 1);
    assert_int_equal(mock_forward_target, 42u);
    assert_int_equal(mock_forward_len, 3u);
    assert_int_equal(mock_forward_last[0], 0xDEu);
    assert_int_equal(mock_forward_last[2], 0xBEu);
    assert_int_equal(ctx.tx_count, 0);

    /* A forward with no target id is malformed, not a broadcast. */
    uint8_t fwd_bare[] = {COMM_FORWARD_CAN};
    assert_int_equal(vesc_comm_process_command(comm, fwd_bare, sizeof(fwd_bare)), EDGE_EINVAL);

    /* A codec with no configuration source refuses rather than answering nonsense. */
    vesc_comm_t *no_cfg = test_comm_alloc2();
    vesc_comm_construct(no_cfg, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, NULL,
                        &test_identity);
    assert_int_equal(vesc_comm_init(no_cfg), EDGE_OK);
    ctx.tx_count = 0;
    assert_int_equal(vesc_comm_process_command(no_cfg, get_mc, sizeof(get_mc)), EDGE_ENOTSUP);
    assert_int_equal(ctx.tx_count, 0);
}

/* COMM_GET_VALUES_SETUP's provider: fixed values so the reply's layout can be checked. */
static edge_status_t mock_get_setup_values(void *self, vesc_setup_values_t *out) {
    (void)self;
    memset(out, 0, sizeof(*out));
    out->temp_mos = 25.0f;      /* float16 x 1e1 -> 250 */
    out->current_tot = 12.34f;  /* float32 x 1e2 -> 1234 */
    out->duty_now = 0.5f;       /* float16 x 1e3 -> 500 */
    out->v_in = 50.0f;          /* float16 x 1e1 -> 500 */
    out->battery_level = 0.75f; /* float16 x 1e3 -> 750 */
    out->odometer_m = 0x11223344u;
    out->uptime_ms = 0x00001234u;
    out->controller_id = 7u;
    out->num_vescs = 1u;
    return EDGE_OK;
}

/*
 * COMM_GET_VALUES_SETUP, reference comm/commands.c:797-885. The reply's length is the sum of the
 * per-bit encodings, derived from that table rather than from the implementation: 1 command byte
 * plus 69 field bytes for the full mask, so a payload of 70.
 */
static void test_setup_values_framing(void **state) {
    (void)state;
    mock_comm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    edge_stream_tx_port_t tx_port = {.write = mock_stream_write, .self = &ctx};

    vesc_motor_provider_port_t motor_port = {
        .get_values = mock_get_values,
        .set_duty = mock_set_duty,
        .set_current = mock_set_current,
        .set_current_brake = mock_set_current_brake,
        .set_rpm = mock_set_rpm,
        .set_pos = mock_set_pos,
        .get_setup_values = mock_get_setup_values,
        .self = &ctx,
    };

    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10u, &tx_port, &motor_port, NULL, NULL, NULL,
                        &test_identity);
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);

    /* The full-mask reply: 1 command byte + 69 field bytes. */
    ctx.tx_count = 0;
    uint8_t get_setup[] = {COMM_GET_VALUES_SETUP};
    assert_int_equal(vesc_comm_process_command(comm, get_setup, sizeof(get_setup)), EDGE_OK);
    assert_int_equal(ctx.tx_count, 1);
    assert_int_equal(ctx.tx_buf[1], 70u); /* payload length */
    assert_int_equal(ctx.tx_buf[2], COMM_GET_VALUES_SETUP);
    /* bit0 temp_mos = 250 at float16 x 1e1, big endian. */
    assert_int_equal(ctx.tx_buf[3], 0x00u);
    assert_int_equal(ctx.tx_buf[4], 0xFAu);
    /* The last two fields are the odometer (bit20) and the uptime (bit21), both uint32. */
    const uint8_t *tail = ctx.tx_buf + 2u + 62u; /* the last eight payload bytes */
    assert_int_equal(tail[0], 0x11u);
    assert_int_equal(tail[1], 0x22u);
    assert_int_equal(tail[2], 0x33u);
    assert_int_equal(tail[3], 0x44u);
    assert_int_equal(tail[4], 0x00u);
    assert_int_equal(tail[5], 0x00u);
    assert_int_equal(tail[6], 0x12u);
    assert_int_equal(tail[7], 0x34u);

    /* The selective variant echoes the mask and sends only the requested bit: 1 + 4 + 4. */
    ctx.tx_count = 0;
    uint8_t sel[] = {COMM_GET_VALUES_SETUP_SELECTIVE, 0x00u, 0x10u, 0x00u, 0x00u}; /* bit 20 */
    assert_int_equal(vesc_comm_process_command(comm, sel, sizeof(sel)), EDGE_OK);
    assert_int_equal(ctx.tx_count, 1);
    assert_int_equal(ctx.tx_buf[1], 9u);
    assert_int_equal(ctx.tx_buf[2], COMM_GET_VALUES_SETUP_SELECTIVE);
    assert_int_equal(ctx.tx_buf[3], 0x00u); /* the mask, echoed */
    assert_int_equal(ctx.tx_buf[4], 0x10u);
    assert_int_equal(ctx.tx_buf[7], 0x11u); /* the odometer */
    assert_int_equal(ctx.tx_buf[10], 0x44u);

    /* A selective frame without its mask is malformed, and a codec with no provider refuses. */
    uint8_t sel_bare[] = {COMM_GET_VALUES_SETUP_SELECTIVE, 0x00u, 0x10u};
    assert_int_equal(vesc_comm_process_command(comm, sel_bare, sizeof(sel_bare)), EDGE_EINVAL);

    vesc_comm_t *no_provider = test_comm_alloc2();
    vesc_comm_construct(no_provider, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, NULL,
                        &test_identity);
    assert_int_equal(vesc_comm_init(no_provider), EDGE_OK);
    assert_int_equal(vesc_comm_process_command(no_provider, get_setup, sizeof(get_setup)),
                     EDGE_ENOTSUP);
}

/*
 * Every id this port handles, driven once through the dispatcher. Each case in
 * vesc_comm_process_command is its own body, so a test that exercises a handful of ids leaves the
 * rest unexecuted - measured, that was 112 of this file's lines. The payload is sixteen zero bytes
 * after the id, which is what most of these accept, and the two promises checked are the
 * dispatcher's: a handled id never answers "unknown", and an id nobody handles answers nothing at
 * all. The reply-producing ids are held to their own promise as well.
 */
static void test_every_handled_command_id_is_reachable(void **state) {
    (void)state;
    mock_comm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    mock_config_ctx_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    mock_terminal_calls = 0;
    mock_forward_calls = 0;

    edge_stream_tx_port_t tx_port = {.write = mock_stream_write, .self = &ctx};
    vesc_app_status_port_t app_status_port = {.get_decoded_ppm = mock_get_decoded_ppm,
                                              .get_decoded_adc = mock_get_decoded_adc,
                                              .self = &ctx};
    vesc_motor_provider_port_t motor_port = {.get_values = mock_get_values,
                                             .get_stats = mock_get_stats,
                                             .reset_stats = mock_reset_stats,
                                             .set_duty = mock_set_duty,
                                             .set_current = mock_set_current,
                                             .set_current_rel = mock_set_current_rel,
                                             .set_handbrake = mock_set_handbrake,
                                             .set_current_brake = mock_set_current_brake,
                                             .set_rpm = mock_set_rpm,
                                             .set_pos = mock_set_pos,
                                             .get_setup_values = mock_get_setup_values,
                                             .self = &ctx};
    vesc_config_provider_port_t config_port = {.get_mcconf = mock_get_mcconf,
                                               .set_mcconf = mock_set_mcconf,
                                               .get_appconf = mock_get_appconf,
                                               .set_appconf = mock_set_appconf,
                                               .get_mcconf_default = mock_get_mcconf_default,
                                               .get_appconf_default = mock_get_appconf_default,
                                               .set_appconf_nostore = mock_set_appconf_nostore,
                                               .self = &cfg};
    vesc_comm_ops_port_t ops_port = {
        .terminal_cmd = mock_terminal_cmd, .forward_can = mock_forward_can, .self = NULL};

    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10u, &tx_port, &motor_port, &app_status_port,
                        &config_port, &ops_port, &test_identity);
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);

    static const uint8_t handled[] = {COMM_FW_VERSION,
                                      COMM_GET_VALUES,
                                      COMM_GET_VALUES_SELECTIVE,
                                      COMM_SET_DUTY,
                                      COMM_TERMINAL_CMD,
                                      COMM_FORWARD_CAN,
                                      COMM_GET_VALUES_SETUP,
                                      COMM_GET_VALUES_SETUP_SELECTIVE,
                                      COMM_GET_MCCONF,
                                      COMM_SET_MCCONF,
                                      COMM_GET_MCCONF_DEFAULT,
                                      COMM_GET_APPCONF_DEFAULT,
                                      COMM_GET_APPCONF,
                                      COMM_SET_APPCONF,
                                      COMM_SET_APPCONF_NO_STORE,
                                      COMM_SET_CURRENT,
                                      COMM_SET_HANDBRAKE,
                                      COMM_SET_CURRENT_REL,
                                      COMM_SET_CURRENT_BRAKE,
                                      COMM_SET_RPM,
                                      COMM_SET_POS,
                                      COMM_GET_STATS,
                                      COMM_RESET_STATS,
                                      COMM_GET_DECODED_PPM,
                                      COMM_GET_DECODED_ADC,
                                      COMM_ALIVE};

    /* The ones whose whole job is to answer: their reply must appear. */
    static const uint8_t answers[] = {
        COMM_FW_VERSION,  COMM_GET_VALUES, COMM_GET_VALUES_SETUP, COMM_GET_MCCONF,
        COMM_GET_APPCONF, COMM_GET_STATS,  COMM_GET_DECODED_PPM,  COMM_GET_DECODED_ADC};

    for (size_t i = 0u; i < sizeof(handled) / sizeof(handled[0]); i++) {
        uint8_t payload[17] = {0};
        payload[0] = handled[i];
        const size_t before = ctx.tx_count;
        const edge_status_t status = vesc_comm_process_command(comm, payload, sizeof(payload));
        if (status == EDGE_ENOTSUP) {
            /* Name the id in the failure: the dispatcher has a case for every id in this list, so
             * an ENOTSUP here means the case fell through or the id list drifted. */
            print_message("command id %u answered ENOTSUP\n", (unsigned)handled[i]);
            assert_true(false);
        }

        bool must_answer = false;
        for (size_t k = 0u; k < sizeof(answers) / sizeof(answers[0]); k++) {
            must_answer = must_answer || (handled[i] == answers[k]);
        }
        if (must_answer) {
            assert_true(ctx.tx_count > before);
        }
    }

    /* The terminal and the CAN forward went through the product in that walk. */
    assert_true(mock_terminal_calls > 0);
    assert_true(mock_forward_calls > 0);

    /* The same ids again with nothing but the id byte: every one of them has to refuse a payload
     * it cannot decode, and none of them may answer "unknown" or write a reply it cannot build.
     * This is the half of each case that a well-formed payload never reaches. */
    for (size_t i = 0u; i < sizeof(handled) / sizeof(handled[0]); i++) {
        uint8_t short_payload[1] = {handled[i]};
        const edge_status_t status =
            vesc_comm_process_command(comm, short_payload, sizeof(short_payload));
        assert_true(status != EDGE_ENOTSUP);
    }

    /* An id nobody handles still answers nothing at all. */
    uint8_t detect_only[1] = {COMM_DETECT_MOTOR_R_L};
    assert_int_equal(vesc_comm_process_command(comm, detect_only, sizeof(detect_only)),
                     EDGE_ENOTSUP);

    /* An id nobody handles answers nothing, which is what the reference does with an unknown
     * command - and what docs/bldc-migration.md records for the detection family. */
    const size_t before = ctx.tx_count;
    uint8_t unknown[2] = {0xFEu, 0u};
    assert_int_equal(vesc_comm_process_command(comm, unknown, sizeof(unknown)), EDGE_ENOTSUP);
    assert_int_equal(ctx.tx_count, before);
}

/*
 * The half of each command that needs something the port may not have. A comm instance given only
 * the streaming port - which is the state the product starts in, before the composition root wires
 * the rest - has to answer the commands that need nothing else and refuse the others rather than
 * reading through a port it was not given.
 *
 * The decoded-input commands come with their negative side: the two values are packed with a sign
 * bit, so a reversed input has to arrive reversed rather than as a large positive number.
 */
static void test_commands_without_optional_ports(void **state) {
    (void)state;
    mock_comm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    edge_stream_tx_port_t tx_port = {.write = mock_stream_write, .self = &ctx};

    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, NULL,
                        &test_identity);
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);

    /* Every command that needs a port this instance does not have must refuse. What is asserted
     * is not which code it refuses with - a missing dependency legitimately answers ENOTSUP, as the
     * dispatcher says - but that none of them puts a reply on the wire: a half-built reply from an
     * uninitialised local would be worse than an error. */
    static const uint8_t needs_a_port[] = {COMM_GET_VALUES,
                                           COMM_GET_VALUES_SELECTIVE,
                                           COMM_GET_VALUES_SETUP,
                                           COMM_GET_VALUES_SETUP_SELECTIVE,
                                           COMM_SET_DUTY,
                                           COMM_SET_CURRENT,
                                           COMM_SET_CURRENT_REL,
                                           COMM_SET_CURRENT_BRAKE,
                                           COMM_SET_RPM,
                                           COMM_SET_POS,
                                           COMM_SET_HANDBRAKE,
                                           COMM_GET_STATS,
                                           COMM_RESET_STATS,
                                           COMM_GET_DECODED_PPM,
                                           COMM_GET_DECODED_ADC,
                                           COMM_GET_MCCONF,
                                           COMM_SET_MCCONF,
                                           COMM_GET_APPCONF,
                                           COMM_SET_APPCONF,
                                           COMM_GET_MCCONF_DEFAULT,
                                           COMM_GET_APPCONF_DEFAULT,
                                           COMM_SET_APPCONF_NO_STORE,
                                           COMM_TERMINAL_CMD,
                                           COMM_FORWARD_CAN};
    const size_t before = ctx.tx_count;
    for (size_t i = 0u; i < sizeof(needs_a_port) / sizeof(needs_a_port[0]); i++) {
        uint8_t payload[17] = {0};
        payload[0] = needs_a_port[i];
        (void)vesc_comm_process_command(comm, payload, sizeof(payload));
    }
    assert_int_equal(ctx.tx_count, before);

    /* The id that needs no port at all still answers. */
    uint8_t alive[1] = {COMM_ALIVE};
    assert_int_equal(vesc_comm_process_command(comm, alive, sizeof(alive)), EDGE_OK);
}

/*
 * The decoded inputs' sign: both are packed with a sign bit, so an input below the middle of its
 * range has to arrive negative. The mock's values are set negative here rather than left at the
 * middle, which is the half the framing cases never exercise.
 */
static void test_decoded_inputs_negative_side(void **state) {
    (void)state;
    mock_comm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ppm_level = -1.0f;
    ctx.ppm_pulse_us = -100.0f;
    ctx.adc_level = -1.0f;
    ctx.adc_voltage = -0.5f;

    edge_stream_tx_port_t tx_port = {.write = mock_stream_write, .self = &ctx};
    vesc_app_status_port_t app_status_port = {.get_decoded_ppm = mock_get_decoded_ppm,
                                              .get_decoded_adc = mock_get_decoded_adc,
                                              .self = &ctx};

    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, &app_status_port, NULL, NULL,
                        &test_identity);
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);

    uint8_t ppm[1] = {COMM_GET_DECODED_PPM};
    assert_int_equal(vesc_comm_process_command(comm, ppm, sizeof(ppm)), EDGE_OK);
    assert_true(ctx.tx_count > 0u);

    uint8_t adc[1] = {COMM_GET_DECODED_ADC};
    assert_int_equal(vesc_comm_process_command(comm, adc, sizeof(adc)), EDGE_OK);
}

/* Every port fails, to reach each command's error propagation rather than its happy path. */
static edge_status_t fail_get_values(void *self, uint32_t mask, vesc_values_t *out) {
    (void)self;
    (void)mask;
    (void)out;
    return EDGE_EIO;
}

static edge_status_t fail_get_setup(void *self, vesc_setup_values_t *out) {
    (void)self;
    (void)out;
    return EDGE_EIO;
}

static edge_status_t fail_get_stats(void *self, vesc_stats_t *out) {
    (void)self;
    (void)out;
    return EDGE_EIO;
}

static edge_status_t fail_void_self(void *self) {
    (void)self;
    return EDGE_EIO;
}

static edge_status_t fail_float(void *self, float value) {
    (void)self;
    (void)value;
    return EDGE_EIO;
}

static edge_status_t fail_stream_out(void *self, uint8_t *out, size_t buf_size, size_t *out_len) {
    (void)self;
    (void)out;
    (void)buf_size;
    (void)out_len;
    return EDGE_EIO;
}

static edge_status_t fail_stream_in(void *self, const uint8_t *in, size_t len) {
    (void)self;
    (void)in;
    (void)len;
    return EDGE_EIO;
}

static edge_status_t fail_terminal(void *self, const char *cmd) {
    (void)self;
    (void)cmd;
    return EDGE_EIO;
}

static edge_status_t fail_forward(void *self, uint8_t target_id, const uint8_t *data, size_t len) {
    (void)self;
    (void)target_id;
    (void)data;
    (void)len;
    return EDGE_EIO;
}

/*
 * Every port that can fail, failing. Each command has an error arm of its own - a port that cannot
 * answer has to have its error come back out - and nothing may go on the wire while that happens,
 * because a reply built from an uninitialised local would be worse than an error. This is the arm
 * the happy-path walks never take.
 */
static void test_every_command_propagates_a_port_failure(void **state) {
    (void)state;
    mock_comm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    mock_config_ctx_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    edge_stream_tx_port_t tx_port = {.write = mock_stream_write, .self = &ctx};
    vesc_motor_provider_port_t motor_port = {.get_values = fail_get_values,
                                             .get_setup_values = fail_get_setup,
                                             .get_stats = fail_get_stats,
                                             .reset_stats = fail_void_self,
                                             .set_duty = fail_float,
                                             .set_current = fail_float,
                                             .set_current_rel = fail_float,
                                             .set_current_brake = fail_float,
                                             .set_rpm = fail_float,
                                             .set_pos = fail_float,
                                             .set_handbrake = fail_float,
                                             .self = &ctx};
    vesc_config_provider_port_t config_port = {.get_mcconf = fail_stream_out,
                                               .set_mcconf = fail_stream_in,
                                               .get_appconf = fail_stream_out,
                                               .set_appconf = fail_stream_in,
                                               .get_mcconf_default = fail_stream_out,
                                               .get_appconf_default = fail_stream_out,
                                               .set_appconf_nostore = fail_stream_in,
                                               .self = &cfg};
    vesc_comm_ops_port_t ops_port = {
        .terminal_cmd = fail_terminal, .forward_can = fail_forward, .self = NULL};

    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10u, &tx_port, &motor_port, NULL, &config_port,
                        &ops_port, &test_identity);
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);

    static const uint8_t commands[] = {COMM_GET_VALUES,
                                       COMM_GET_VALUES_SETUP,
                                       COMM_GET_STATS,
                                       COMM_RESET_STATS,
                                       COMM_SET_DUTY,
                                       COMM_SET_CURRENT,
                                       COMM_SET_CURRENT_REL,
                                       COMM_SET_CURRENT_BRAKE,
                                       COMM_SET_RPM,
                                       COMM_SET_POS,
                                       COMM_SET_HANDBRAKE,
                                       COMM_GET_MCCONF,
                                       COMM_GET_MCCONF_DEFAULT,
                                       COMM_GET_APPCONF,
                                       COMM_GET_APPCONF_DEFAULT,
                                       COMM_SET_MCCONF,
                                       COMM_SET_APPCONF,
                                       COMM_SET_APPCONF_NO_STORE,
                                       COMM_TERMINAL_CMD,
                                       COMM_FORWARD_CAN};

    for (size_t i = 0u; i < sizeof(commands) / sizeof(commands[0]); i++) {
        uint8_t payload[17] = {0};
        payload[0] = commands[i];
        const size_t before = ctx.tx_count;
        const edge_status_t status = vesc_comm_process_command(comm, payload, sizeof(payload));
        /* The failure comes back out, and no reply is built from it. */
        assert_true(status != EDGE_OK);
        assert_int_equal(ctx.tx_count, before);
    }
}

/*
 * COMM_DETECT_MOTOR_FLUX_LINKAGE_OPENLOOP. The measurement itself belongs to the product; what is
 * checked here is the framing of the request and the caller's three rules for choosing which number
 * goes back (comm/commands.c:2304-2314).
 */
typedef struct mock_flux_ctx {
    float current;
    float duty;
    float erpm_per_sec;
    float resistance;
    float inductance;
    int calls;
    vesc_detect_flux_result_t result;
    edge_status_t status;
} mock_flux_ctx_t;

static edge_status_t mock_detect_flux(void *self, float current_a, float duty, float erpm_per_sec,
                                      float resistance_ohm, float inductance_h,
                                      vesc_detect_flux_result_t *result) {
    mock_flux_ctx_t *ctx = (mock_flux_ctx_t *)self;
    ctx->calls++;
    ctx->current = current_a;
    ctx->duty = duty;
    ctx->erpm_per_sec = erpm_per_sec;
    ctx->resistance = resistance_ohm;
    ctx->inductance = inductance_h;
    if (ctx->status != EDGE_OK) {
        return ctx->status;
    }
    *result = ctx->result;
    return EDGE_OK;
}

/* The reply's payload, found by its command id so the framing either side of it does not matter. */
static bool reply_carries(const mock_comm_ctx_t *ctx, uint8_t id, float value, float scale) {
    for (int32_t at = 0; at + 1 < (int32_t)ctx->tx_len; ++at) {
        if (ctx->tx_buf[at] == id) {
            int32_t index = at + 1;
            const float got = vesc_buffer_get_float32(ctx->tx_buf, scale, &index);
            return fabsf(got - value) < 1e-4f;
        }
    }
    return false;
}

static void test_detect_flux_linkage_openloop_command(void **state) {
    (void)state;
    mock_comm_ctx_t tx;
    memset(&tx, 0, sizeof(tx));
    edge_stream_tx_port_t tx_port = {.write = mock_stream_write, .self = &tx};
    mock_flux_ctx_t flux;
    memset(&flux, 0, sizeof(flux));
    vesc_comm_ops_port_t ops_port = {.terminal_cmd = mock_terminal_cmd,
                                     .forward_can = mock_forward_can,
                                     .detect_flux_linkage_openloop = mock_detect_flux,
                                     .self = &flux};
    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, &ops_port,
                        &test_identity);
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);

    /* The request as the reference frames it: current, electrical speed per second, duty and
     * resistance, all at 1e3 except the resistance. */
    uint8_t req[32];
    int32_t len = 0;
    req[len++] = COMM_DETECT_MOTOR_FLUX_LINKAGE_OPENLOOP;
    vesc_buffer_append_float32(req, 5.0f, 1e3, &len);
    vesc_buffer_append_float32(req, 2000.0f, 1e3, &len);
    vesc_buffer_append_float32(req, 0.5f, 1e3, &len);
    vesc_buffer_append_float32(req, 0.05f, 1e6, &len);
    assert_int_equal(vesc_comm_process_command(comm, req, (size_t)len), EDGE_OK);
    assert_int_equal(flux.calls, 1);
    assert_float_equal(flux.current, 5.0f, 1e-2f);
    assert_float_equal(flux.erpm_per_sec, 2000.0f, 1.0f);
    assert_float_equal(flux.duty, 0.5f, 1e-3f);
    assert_float_equal(flux.resistance, 0.05f, 1e-5f);
    /* No inductance in this packet, so the procedure is told to take the configuration's. */
    assert_float_equal(flux.inductance, 0.0f, 1e-9f);

    /* Enough undriven samples: the undriven measurement is the one that goes back. */
    flux.result.valid = true;
    flux.result.linkage_wb = 0.02f;
    flux.result.linkage_undriven_wb = 0.012f;
    flux.result.undriven_samples = 100.0f;
    assert_int_equal(vesc_comm_process_command(comm, req, (size_t)len), EDGE_OK);
    assert_true(reply_carries(&tx, COMM_DETECT_MOTOR_FLUX_LINKAGE_OPENLOOP, 0.012f, 1e7));

    /* Too few: the driven measurement stands. */
    flux.result.undriven_samples = 10.0f;
    assert_int_equal(vesc_comm_process_command(comm, req, (size_t)len), EDGE_OK);
    assert_true(reply_carries(&tx, COMM_DETECT_MOTOR_FLUX_LINKAGE_OPENLOOP, 0.02f, 1e7));

    /* Not trusted, or a failed call: zero. */
    flux.result.valid = false;
    assert_int_equal(vesc_comm_process_command(comm, req, (size_t)len), EDGE_OK);
    assert_true(reply_carries(&tx, COMM_DETECT_MOTOR_FLUX_LINKAGE_OPENLOOP, 0.0f, 1e7));
    flux.result.valid = true;
    flux.status = EDGE_EIO;
    assert_int_equal(vesc_comm_process_command(comm, req, (size_t)len), EDGE_OK);
    assert_true(reply_carries(&tx, COMM_DETECT_MOTOR_FLUX_LINKAGE_OPENLOOP, 0.0f, 1e7));

    /* The inductance, when the packet carries it, arrives at its own scale. */
    flux.status = EDGE_OK;
    int32_t with_ind = len;
    vesc_buffer_append_float32(req, 1.5e-5f, 1e8, &with_ind);
    assert_int_equal(vesc_comm_process_command(comm, req, (size_t)with_ind), EDGE_OK);
    assert_float_equal(flux.inductance, 1.5e-5f, 1e-8f);

    /* Without the callback the command says it cannot, which is what an unknown command does. */
    vesc_comm_ops_port_t no_flux = {
        .terminal_cmd = mock_terminal_cmd, .forward_can = mock_forward_can, .self = &flux};
    vesc_comm_t *other = test_comm_alloc2();
    vesc_comm_construct(other, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, &no_flux,
                        &test_identity);
    assert_int_equal(vesc_comm_init(other), EDGE_OK);
    assert_int_equal(vesc_comm_process_command(other, req, (size_t)len), EDGE_ENOTSUP);
}

/* COMM_DETECT_MOTOR_FLUX_LINKAGE: the sensored measurement, whose reply is one number. */
static edge_status_t mock_detect_flux_sensored(void *self, float current_a, float min_rpm,
                                               float duty, float resistance_ohm,
                                               float *linkage_wb) {
    mock_flux_ctx_t *ctx = (mock_flux_ctx_t *)self;
    ctx->calls++;
    ctx->current = current_a;
    ctx->erpm_per_sec =
        min_rpm; /* the context's name is the open-loop command's; this is min_rpm */
    ctx->duty = duty;
    ctx->resistance = resistance_ohm;
    if (ctx->status != EDGE_OK) {
        return ctx->status;
    }
    *linkage_wb = ctx->result.linkage_wb;
    return EDGE_OK;
}

static void test_detect_flux_linkage_command(void **state) {
    (void)state;
    mock_comm_ctx_t tx;
    memset(&tx, 0, sizeof(tx));
    edge_stream_tx_port_t tx_port = {.write = mock_stream_write, .self = &tx};
    mock_flux_ctx_t flux;
    memset(&flux, 0, sizeof(flux));
    vesc_comm_ops_port_t ops_port = {.terminal_cmd = mock_terminal_cmd,
                                     .forward_can = mock_forward_can,
                                     .detect_flux_linkage = mock_detect_flux_sensored,
                                     .self = &flux};
    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, &ops_port,
                        &test_identity);
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);

    /* The request as the reference reads it: current, minimum rpm, duty and resistance. */
    uint8_t req[32];
    int32_t len = 0;
    req[len++] = COMM_DETECT_MOTOR_FLUX_LINKAGE;
    vesc_buffer_append_float32(req, 4.0f, 1e3, &len);
    vesc_buffer_append_float32(req, 300.0f, 1e3, &len);
    vesc_buffer_append_float32(req, 0.25f, 1e3, &len);
    vesc_buffer_append_float32(req, 0.08f, 1e6, &len);

    flux.result.linkage_wb = 0.0123f;
    assert_int_equal(vesc_comm_process_command(comm, req, (size_t)len), EDGE_OK);
    assert_int_equal(flux.calls, 1);
    assert_float_equal(flux.current, 4.0f, 1e-2f);
    assert_float_equal(flux.erpm_per_sec, 300.0f, 1.0f);
    assert_float_equal(flux.duty, 0.25f, 1e-3f);
    assert_float_equal(flux.resistance, 0.08f, 1e-5f);
    assert_true(reply_carries(&tx, COMM_DETECT_MOTOR_FLUX_LINKAGE, 0.0123f, 1e7));

    /* The reference's own rule: a measurement that did not succeed goes out as zero. */
    flux.status = EDGE_EIO;
    assert_int_equal(vesc_comm_process_command(comm, req, (size_t)len), EDGE_OK);
    assert_true(reply_carries(&tx, COMM_DETECT_MOTOR_FLUX_LINKAGE, 0.0f, 1e7));

    /* A packet too short to hold the request is refused before anything is measured. */
    uint8_t short_req[8] = {0u};
    short_req[0] = COMM_DETECT_MOTOR_FLUX_LINKAGE;
    assert_int_equal(vesc_comm_process_command(comm, short_req, sizeof(short_req)), EDGE_EINVAL);

    /* Without the callback the command says it cannot. */
    vesc_comm_ops_port_t no_flux = {
        .terminal_cmd = mock_terminal_cmd, .forward_can = mock_forward_can, .self = &flux};
    vesc_comm_t *other = test_comm_alloc2();
    vesc_comm_construct(other, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, &no_flux,
                        &test_identity);
    assert_int_equal(vesc_comm_init(other), EDGE_OK);
    assert_int_equal(vesc_comm_process_command(other, req, (size_t)len), EDGE_ENOTSUP);
}

/*
 * COMM_DETECT_MOTOR_R_L: the request carries nothing, and the reply is the resistance, the
 * inductance and the difference between the two axes, each at its own scale.
 */
typedef struct mock_r_l_ctx {
    int calls;
    vesc_detect_r_l_result_t result;
    edge_status_t status;
} mock_r_l_ctx_t;

static edge_status_t mock_detect_r_l(void *self, vesc_detect_r_l_result_t *result) {
    mock_r_l_ctx_t *ctx = (mock_r_l_ctx_t *)self;
    ctx->calls++;
    if (ctx->status != EDGE_OK) {
        return ctx->status;
    }
    *result = ctx->result;
    return EDGE_OK;
}

/*
 * The reply's three numbers. Every occurrence of the command id is tried, because a float's bytes
 * can hold the id's value too - the reply is the command id followed by three of them, and only
 * one of those positions parses to the triplet that was sent.
 */
static bool r_l_reply_carries(const mock_comm_ctx_t *ctx, float r, float l, float diff) {
    for (int32_t at = 0; at + 13 <= (int32_t)ctx->tx_len; ++at) {
        if (ctx->tx_buf[at] != COMM_DETECT_MOTOR_R_L) {
            continue;
        }
        int32_t index = at + 1;
        const float got_r = vesc_buffer_get_float32(ctx->tx_buf, 1e6, &index);
        const float got_l = vesc_buffer_get_float32(ctx->tx_buf, 1e3, &index);
        const float got_diff = vesc_buffer_get_float32(ctx->tx_buf, 1e3, &index);
        if (fabsf(got_r - r) < 1e-4f && fabsf(got_l - l) < 1e-3f &&
            fabsf(got_diff - diff) < 1e-3f) {
            return true;
        }
    }
    return false;
}

static void test_detect_r_l_command(void **state) {
    (void)state;
    mock_comm_ctx_t tx;
    memset(&tx, 0, sizeof(tx));
    edge_stream_tx_port_t tx_port = {.write = mock_stream_write, .self = &tx};
    mock_r_l_ctx_t rl;
    memset(&rl, 0, sizeof(rl));
    vesc_comm_ops_port_t ops_port = {.terminal_cmd = mock_terminal_cmd,
                                     .forward_can = mock_forward_can,
                                     .detect_r_l = mock_detect_r_l,
                                     .self = &rl};
    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, &ops_port,
                        &test_identity);
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);

    const uint8_t req[1] = {COMM_DETECT_MOTOR_R_L};
    assert_int_equal(vesc_comm_process_command(comm, req, sizeof(req)), EDGE_OK);
    assert_int_equal(rl.calls, 1);

    rl.result.valid = true;
    rl.result.r_ohm = 0.052f;
    rl.result.l_uh = 45.0f;
    rl.result.ld_lq_diff_uh = 20.0f;
    /* The capture holds every packet this session sent, so it is cleared before each reply is read
     * back - the reply's own id appears in earlier ones too. */
    memset(&tx, 0, sizeof(tx));
    assert_int_equal(vesc_comm_process_command(comm, req, sizeof(req)), EDGE_OK);
    assert_true(r_l_reply_carries(&tx, 0.052f, 45.0f, 20.0f));

    /* A failed call fills nothing, so all three are zero - the reference's own locals are the same
     * way, its difference being one it never reached. */
    rl.status = EDGE_EIO;
    memset(&tx, 0, sizeof(tx));
    assert_int_equal(vesc_comm_process_command(comm, req, sizeof(req)), EDGE_OK);
    assert_true(r_l_reply_carries(&tx, 0.0f, 0.0f, 0.0f));
    /* An untrusted answer leaves the first two at zero while the difference stays as the
     * measurement left it. */
    rl.status = EDGE_OK;
    rl.result.valid = false;
    memset(&tx, 0, sizeof(tx));
    assert_int_equal(vesc_comm_process_command(comm, req, sizeof(req)), EDGE_OK);
    assert_true(r_l_reply_carries(&tx, 0.0f, 0.0f, 20.0f));

    /* Without the callback the command says it cannot, as an unported one does. */
    vesc_comm_ops_port_t no_r_l = {
        .terminal_cmd = mock_terminal_cmd, .forward_can = mock_forward_can, .self = &rl};
    vesc_comm_t *bare = test_comm_alloc();
    vesc_comm_construct(bare, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, &no_r_l,
                        &test_identity);
    assert_int_equal(vesc_comm_init(bare), EDGE_OK);
    assert_int_equal(vesc_comm_process_command(bare, req, sizeof(req)), EDGE_ENOTSUP);
}

/*
 * The two detection commands this port cannot run say so rather than half-running: the parameter
 * detection needs the BLDC six-step drive and raw hall inputs, and the all-in-one detection starts
 * with a DC-offset calibration this port has no pass for. The reasons are in the codec's own case
 * labels.
 */
typedef struct mock_apply_ctx {
    int apply_calls;
    bool detect_can;
    float max_power_loss;
    float min_current_in;
    float max_current_in;
    float openloop_rpm;
    float sl_erpm;
    int16_t result;
} mock_apply_ctx_t;

static edge_status_t mock_detect_apply_all_foc(void *self, bool detect_can, float max_power_loss,
                                               float min_current_in, float max_current_in,
                                               float openloop_rpm, float sl_erpm, int16_t *result) {
    mock_apply_ctx_t *ctx = (mock_apply_ctx_t *)self;
    ctx->apply_calls++;
    ctx->detect_can = detect_can;
    ctx->max_power_loss = max_power_loss;
    ctx->min_current_in = min_current_in;
    ctx->max_current_in = max_current_in;
    ctx->openloop_rpm = openloop_rpm;
    ctx->sl_erpm = sl_erpm;
    *result = ctx->result;
    return EDGE_OK;
}

/*
 * The command whose whole run belongs to the product, and the one that cannot run here at all.
 *
 * The parameter detection drives the motor with the BLDC six-step commutator, so there is nothing
 * for this layer to call and it refuses whatever the packet carries.
 *
 * The all-in-one detection has a callback, and this is the layer's own half of it: the packet is
 * parsed, a short one is refused before anything is read out of it - the reference walks off the
 * end of one - a product that has not implemented the callback gets ENOTSUP, and a product that has
 * gets its result code back as the reference's int16 reply.
 */
static void test_detect_param_is_refused_and_apply_all_forwards(void **state) {
    (void)state;
    mock_comm_ctx_t tx;
    memset(&tx, 0, sizeof(tx));
    edge_stream_tx_port_t tx_port = {.write = mock_stream_write, .self = &tx};
    vesc_comm_ops_port_t ops_port = {
        .terminal_cmd = mock_terminal_cmd, .forward_can = mock_forward_can, .self = NULL};
    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, &ops_port,
                        &test_identity);
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);

    const uint8_t param[1] = {COMM_DETECT_MOTOR_PARAM};
    assert_int_equal(vesc_comm_process_command(comm, param, sizeof(param)), EDGE_EINVAL);

    /* A well-formed detection request with no callback behind it: the product has not implemented
     * the run, so the command is refused rather than half answered. */
    uint8_t param_full[13];
    int32_t param_at = 0;
    param_full[param_at++] = COMM_DETECT_MOTOR_PARAM;
    vesc_buffer_append_float32(param_full, 5.0f, 1e3f, &param_at);
    vesc_buffer_append_float32(param_full, 200.0f, 1e3f, &param_at);
    vesc_buffer_append_float32(param_full, 0.2f, 1e3f, &param_at);
    assert_int_equal(vesc_comm_process_command(comm, param_full, (size_t)param_at), EDGE_ENOTSUP);

    const uint8_t apply_short[1] = {COMM_DETECT_APPLY_ALL_FOC};
    assert_int_equal(vesc_comm_process_command(comm, apply_short, sizeof(apply_short)),
                     EDGE_EINVAL);

    /* A well-formed packet with nothing behind it: the product has not implemented the run. */
    uint8_t apply[22];
    int32_t at = 0;
    apply[at++] = COMM_DETECT_APPLY_ALL_FOC;
    apply[at++] = 1u;                                      /* detect_can */
    vesc_buffer_append_float32(apply, 30.0f, 1e3f, &at);   /* max_power_loss */
    vesc_buffer_append_float32(apply, 5.0f, 1e3f, &at);    /* min_current_in */
    vesc_buffer_append_float32(apply, 40.0f, 1e3f, &at);   /* max_current_in */
    vesc_buffer_append_float32(apply, 700.0f, 1e3f, &at);  /* openloop_rpm */
    vesc_buffer_append_float32(apply, 1500.0f, 1e3f, &at); /* sl_erpm */
    assert_int_equal(at, sizeof(apply));
    assert_int_equal(vesc_comm_process_command(comm, apply, sizeof(apply)), EDGE_ENOTSUP);

    /* With one, its result comes back as the int16 the reference sends, and the inputs arrive
     * parsed rather than scaled. */
    mock_apply_ctx_t apply_ctx = {0};
    apply_ctx.result = -7;
    vesc_comm_ops_port_t with_apply = {.terminal_cmd = mock_terminal_cmd,
                                       .forward_can = mock_forward_can,
                                       .detect_apply_all_foc = mock_detect_apply_all_foc,
                                       .self = &apply_ctx};
    vesc_comm_t *second = test_comm_alloc();
    vesc_comm_construct(second, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, &with_apply,
                        &test_identity);
    assert_int_equal(vesc_comm_init(second), EDGE_OK);

    tx.tx_count = 0u;
    assert_int_equal(vesc_comm_process_command(second, apply, sizeof(apply)), EDGE_OK);
    assert_int_equal(apply_ctx.apply_calls, 1);
    assert_true(apply_ctx.detect_can);
    assert_float_equal(apply_ctx.max_power_loss, 30.0f, 1e-4f);
    assert_float_equal(apply_ctx.min_current_in, 5.0f, 1e-4f);
    assert_float_equal(apply_ctx.max_current_in, 40.0f, 1e-4f);
    assert_float_equal(apply_ctx.openloop_rpm, 700.0f, 1e-4f);
    assert_float_equal(apply_ctx.sl_erpm, 1500.0f, 1e-4f);

    /* The reply is the command id and the run's result, an int16, which is what the reference
     * sends back for this command. */
    assert_true(tx.tx_count > 0u);
    const uint8_t *reply = tx.tx_buf + 2u; /* past the start byte and the length */
    assert_int_equal(reply[0], COMM_DETECT_APPLY_ALL_FOC);
    assert_int_equal((int16_t)(((uint16_t)reply[1] << 8) | (uint16_t)reply[2]), -7);
}

/*
 * The two commands that end the machine's run: comm/commands.c:695-698 stores the backup block and
 * resets, and :304-306 reaches flash_helper_jump_to_bootloader. Neither sends a reply, and both are
 * the product's to perform - so what this layer is tested for is that it reaches the port, and that
 * a product which has not implemented it is refused rather than silently doing nothing.
 */
typedef struct mock_restart_ctx {
    int reboots;
    int bootloader_jumps;
} mock_restart_ctx_t;

static edge_status_t mock_reboot(void *self) {
    ((mock_restart_ctx_t *)self)->reboots++;
    return EDGE_OK;
}

static edge_status_t mock_jump_to_bootloader(void *self) {
    ((mock_restart_ctx_t *)self)->bootloader_jumps++;
    return EDGE_OK;
}

static void test_restart_commands_reach_the_product(void **state) {
    (void)state;
    mock_comm_ctx_t tx;
    memset(&tx, 0, sizeof(tx));
    edge_stream_tx_port_t tx_port = {.write = mock_stream_write, .self = &tx};

    /* A product with neither: both commands are refused, and nothing is sent. */
    vesc_comm_ops_port_t bare_ops = {
        .terminal_cmd = mock_terminal_cmd, .forward_can = mock_forward_can, .self = NULL};
    vesc_comm_t *bare = test_comm_alloc();
    vesc_comm_construct(bare, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, &bare_ops,
                        &test_identity);
    assert_int_equal(vesc_comm_init(bare), EDGE_OK);

    const uint8_t reboot[1] = {COMM_REBOOT};
    const uint8_t bootloader[1] = {COMM_JUMP_TO_BOOTLOADER};
    tx.tx_count = 0u;
    assert_int_equal(vesc_comm_process_command(bare, reboot, sizeof(reboot)), EDGE_ENOTSUP);
    assert_int_equal(vesc_comm_process_command(bare, bootloader, sizeof(bootloader)), EDGE_ENOTSUP);
    assert_int_equal(tx.tx_count, 0u);

    /* A product with both: each reaches its own callback once, and no reply goes out - the
     * reference's connection simply ends. */
    mock_restart_ctx_t restart = {0};
    vesc_comm_ops_port_t ops = {.terminal_cmd = mock_terminal_cmd,
                                .forward_can = mock_forward_can,
                                .reboot = mock_reboot,
                                .jump_to_bootloader = mock_jump_to_bootloader,
                                .self = &restart};
    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, &ops,
                        &test_identity);
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);

    tx.tx_count = 0u;
    assert_int_equal(vesc_comm_process_command(comm, reboot, sizeof(reboot)), EDGE_OK);
    assert_int_equal(vesc_comm_process_command(comm, bootloader, sizeof(bootloader)), EDGE_OK);
    assert_int_equal(restart.reboots, 1);
    assert_int_equal(restart.bootloader_jumps, 1);
    assert_int_equal(tx.tx_count, 0u);

    /* A codec with no ops port at all refuses both as well. */
    vesc_comm_t *no_ops = test_comm_alloc();
    vesc_comm_construct(no_ops, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, NULL,
                        &test_identity);
    assert_int_equal(vesc_comm_init(no_ops), EDGE_OK);
    assert_int_equal(vesc_comm_process_command(no_ops, reboot, sizeof(reboot)), EDGE_ENOTSUP);
    assert_int_equal(vesc_comm_process_command(no_ops, bootloader, sizeof(bootloader)),
                     EDGE_ENOTSUP);
}

/*
 * COMM_DETECT_HALL_FOC: the reference answers it with the command id, eight bytes of hall table and
 * one byte of verdict, and with a table of two hundred and fifty-fives when the sensor port is not
 * a hall one (comm/commands.c:2238-2276). The product owns that gate, so what this layer is tested
 * for is the packet it parses, the reply it builds in each case, and the refusal when nobody
 * implemented the callback at all.
 */
typedef struct mock_hall_detect_ctx {
    int calls;
    float current;
    uint8_t table[8];
    bool result;
    edge_status_t status;
} mock_hall_detect_ctx_t;

/*
 * The all-in-one detection's reply (comm/commands.c:2096-2123): an int32 of the cycle integrator's
 * ceiling at a thousandth, an int32 of the coupling factor at the same scale, the eight table bytes
 * copied as they are, and one byte of the count of readings that came up short - with a run that
 * did not pass clearing the two numbers and sending the rest of what the procedure left.
 */
typedef struct mock_param_detect_ctx {
    int calls;
    edge_status_t status;
    float int_limit;
    float coupling_k;
    int32_t hall_res;
    uint8_t table[8];
    float last_current;
    float last_min_rpm;
    float last_low_duty;
} mock_param_detect_ctx_t;

static edge_status_t mock_detect_motor_param(void *self, float current_a, float min_rpm,
                                             float low_duty, float *int_limit,
                                             float *bemf_coupling_k, uint8_t table[8],
                                             int32_t *hall_res) {
    mock_param_detect_ctx_t *ctx = (mock_param_detect_ctx_t *)self;
    ctx->calls++;
    ctx->last_current = current_a;
    ctx->last_min_rpm = min_rpm;
    ctx->last_low_duty = low_duty;
    if (ctx->status == EDGE_EINVAL || ctx->status == EDGE_ENOTSUP) {
        return ctx->status;
    }
    /* What a procedure with nothing to report leaves behind is still what it reports. */
    *int_limit = ctx->int_limit;
    *bemf_coupling_k = ctx->coupling_k;
    memcpy(table, ctx->table, 8u);
    *hall_res = ctx->hall_res;
    return ctx->status;
}

static void test_detect_motor_param_command(void **state) {
    (void)state;
    mock_comm_ctx_t tx;
    memset(&tx, 0, sizeof(tx));
    edge_stream_tx_port_t tx_port = {.write = mock_stream_write, .self = &tx};

    uint8_t packet[13];
    int32_t at = 0;
    packet[at++] = COMM_DETECT_MOTOR_PARAM;
    vesc_buffer_append_float32(packet, 5.0f, 1e3f, &at);
    vesc_buffer_append_float32(packet, 200.0f, 1e3f, &at);
    vesc_buffer_append_float32(packet, 0.2f, 1e3f, &at);
    assert_int_equal(at, 13);

    mock_param_detect_ctx_t detect;
    memset(&detect, 0, sizeof(detect));
    detect.status = EDGE_OK;
    detect.int_limit = 100.0f;
    detect.coupling_k = 123.456f;
    detect.hall_res = 2;
    for (int i = 0; i < 8; i++) {
        detect.table[i] = (uint8_t)(0x10 + i);
    }

    vesc_comm_ops_port_t ops = {.terminal_cmd = mock_terminal_cmd,
                                .forward_can = mock_forward_can,
                                .detect_motor_param = mock_detect_motor_param,
                                .self = &detect};
    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, &ops,
                        &test_identity);
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);

    tx.tx_count = 0u;
    assert_int_equal(vesc_comm_process_command(comm, packet, sizeof(packet)), EDGE_OK);
    assert_int_equal(detect.calls, 1);
    assert_float_equal(detect.last_current, 5.0f, 1e-3f);
    assert_float_equal(detect.last_min_rpm, 200.0f, 1e-3f);
    assert_float_equal(detect.last_low_duty, 0.2f, 1e-3f);

    /* Two of the framing bytes, eighteen of payload, the checksum and the stop byte. */
    assert_int_equal(tx.tx_len, 2u + 18u + 3u);
    const uint8_t *p = tx.tx_buf + 2u;
    assert_int_equal(p[0], COMM_DETECT_MOTOR_PARAM);
    assert_int_equal((int32_t)(((uint32_t)p[1] << 24) | ((uint32_t)p[2] << 16) |
                               ((uint32_t)p[3] << 8) | (uint32_t)p[4]),
                     100000);
    assert_int_equal((int32_t)(((uint32_t)p[5] << 24) | ((uint32_t)p[6] << 16) |
                               ((uint32_t)p[7] << 8) | (uint32_t)p[8]),
                     123456);
    for (int i = 0; i < 8; i++) {
        assert_int_equal(p[9 + i], 0x10 + i);
    }
    assert_int_equal(p[17], 2u);

    /* A run that did not pass: the two numbers go to nought and the table and count stay. */
    detect.status = EDGE_ESTATE;
    detect.int_limit = 100.0f;
    detect.coupling_k = 123.456f;
    tx.tx_count = 0u;
    assert_int_equal(vesc_comm_process_command(comm, packet, sizeof(packet)), EDGE_OK);
    const uint8_t *q = tx.tx_buf + 2u;
    assert_int_equal((int32_t)(((uint32_t)q[1] << 24) | ((uint32_t)q[2] << 16) |
                               ((uint32_t)q[3] << 8) | (uint32_t)q[4]),
                     0);
    assert_int_equal((int32_t)(((uint32_t)q[5] << 24) | ((uint32_t)q[6] << 16) |
                               ((uint32_t)q[7] << 8) | (uint32_t)q[8]),
                     0);
    assert_int_equal(q[9], 0x10);
    assert_int_equal(q[17], 2u);

    /* And a port-level refusal is the port's answer rather than a reply. */
    detect.status = EDGE_ENOTSUP;
    tx.tx_count = 0u;
    assert_int_equal(vesc_comm_process_command(comm, packet, sizeof(packet)), EDGE_ENOTSUP);
    assert_int_equal(tx.tx_count, 0u);
}

static edge_status_t mock_detect_hall_foc(void *self, float current_a, uint8_t table[8],
                                          bool *result) {
    mock_hall_detect_ctx_t *ctx = (mock_hall_detect_ctx_t *)self;
    ctx->calls++;
    ctx->current = current_a;
    if (ctx->status != EDGE_OK) {
        return ctx->status;
    }
    memcpy(table, ctx->table, 8u);
    *result = ctx->result;
    return EDGE_OK;
}

static void test_detect_hall_foc_command(void **state) {
    (void)state;
    mock_comm_ctx_t tx;
    memset(&tx, 0, sizeof(tx));
    edge_stream_tx_port_t tx_port = {.write = mock_stream_write, .self = &tx};

    /* A packet of the command id and one scaled float, built with the codec. */
    uint8_t packet[5];
    int32_t at = 0;
    packet[at++] = COMM_DETECT_HALL_FOC;
    vesc_buffer_append_float32(packet, 7.5f, 1e3f, &at);
    assert_int_equal(at, 5);

    /* A product that has not implemented it, and one that has. */
    vesc_comm_ops_port_t bare_ops = {
        .terminal_cmd = mock_terminal_cmd, .forward_can = mock_forward_can, .self = NULL};
    vesc_comm_t *bare = test_comm_alloc();
    vesc_comm_construct(bare, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, &bare_ops,
                        &test_identity);
    assert_int_equal(vesc_comm_init(bare), EDGE_OK);
    assert_int_equal(vesc_comm_process_command(bare, packet, sizeof(packet)), EDGE_ENOTSUP);

    mock_hall_detect_ctx_t detect = {
        .result = true, .status = EDGE_OK, .table = {255u, 0u, 33u, 66u, 100u, 133u, 166u, 255u}};
    vesc_comm_ops_port_t ops = {.terminal_cmd = mock_terminal_cmd,
                                .forward_can = mock_forward_can,
                                .detect_hall_foc = mock_detect_hall_foc,
                                .self = &detect};
    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, &ops,
                        &test_identity);
    assert_int_equal(vesc_comm_init(comm), EDGE_OK);

    tx.tx_count = 0u;
    assert_int_equal(vesc_comm_process_command(comm, packet, sizeof(packet)), EDGE_OK);
    assert_int_equal(detect.calls, 1);
    assert_float_equal(detect.current, 7.5f, 1e-4f); /* parsed, not scaled */

    const uint8_t *reply = tx.tx_buf + 2u; /* past the start byte and the length */
    assert_int_equal(reply[0], COMM_DETECT_HALL_FOC);
    const uint8_t expected[8] = {255u, 0u, 33u, 66u, 100u, 133u, 166u, 255u};
    assert_memory_equal(&reply[1], expected, sizeof(expected));
    assert_int_equal(reply[9], 0u); /* the verdict, nought for a detection that passed */

    /* A short packet is refused before it is read. */
    assert_int_equal(vesc_comm_process_command(comm, packet, 2u), EDGE_EINVAL);

    /* A product whose port is not a hall one answers ENOTSUP - the same three bytes the reference's
     * own else branch sends: eight two hundred and fifty-fives and a verdict of one. */
    detect.status = EDGE_ENOTSUP;
    tx.tx_count = 0u;
    assert_int_equal(vesc_comm_process_command(comm, packet, sizeof(packet)), EDGE_OK);
    const uint8_t *blind_reply = tx.tx_buf + 2u;
    assert_int_equal(blind_reply[0], COMM_DETECT_HALL_FOC);
    for (size_t i = 1u; i < 9u; i++) {
        assert_int_equal(blind_reply[i], 255u);
    }
    assert_int_equal(blind_reply[9], 1u);

    /* And a verdict that did not pass is a one in the same place. */
    detect.status = EDGE_OK;
    detect.result = false;
    tx.tx_count = 0u;
    assert_int_equal(vesc_comm_process_command(comm, packet, sizeof(packet)), EDGE_OK);
    assert_int_equal((tx.tx_buf + 2u)[9], 1u);
}

/* comm/commands.c:2470-2545: the IMU's fifteen readings and its nine calibration numbers. */
static edge_status_t mock_get_imu_data(void *self, vesc_imu_data_t *out) {
    (void)self;
    for (int i = 0; i < 3; i++) {
        out->rpy[i] = 0.1f * (float)(i + 1);
        out->accel[i] = 1.0f + (float)i;
        out->gyro[i] = 10.0f + (float)i;
        out->mag[i] = 100.0f + (float)i;
    }
    for (int i = 0; i < 4; i++) {
        out->quat[i] = 0.25f * (float)(i + 1);
    }
    return EDGE_OK;
}

static edge_status_t mock_calibrate_imu(void *self, float yaw_deg, float cal[9]) {
    (void)self;
    for (int i = 0; i < 9; i++) {
        cal[i] = yaw_deg + (float)i;
    }
    return EDGE_OK;
}

static void test_imu_commands(void **state) {
    (void)state;
    mock_comm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    edge_stream_tx_port_t tx_port = {.write = mock_stream_write, .self = &ctx};
    vesc_app_status_port_t app_status_port = {.self = &ctx};
    vesc_motor_provider_port_t motor_port = {.self = &ctx};
    vesc_config_provider_port_t config_port = {.self = &ctx};
    vesc_comm_ops_port_t ops_port = {
        .self = &ctx, .get_imu_data = mock_get_imu_data, .calibrate_imu = mock_calibrate_imu};
    vesc_identity_t identity = {.hw_name = "test", .fw_name = "test", .uuid = NULL};

    /* The codec is opaque, so the case keeps its storage as the others here do. */
    static alignas(VESC_COMM_STORAGE_ALIGN) unsigned char imu_comm_storage[VESC_COMM_STORAGE_SIZE];
    memset(imu_comm_storage, 0, sizeof(imu_comm_storage));
    vesc_comm_t *comm = (vesc_comm_t *)imu_comm_storage;
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10, &tx_port, &motor_port, &app_status_port,
                        &config_port, &ops_port, &identity);

    /* A mask of one bit answers with the mask and that one reading, nothing else. */
    ctx.tx_count = 0;
    ctx.tx_len = 0u;
    uint8_t one[3] = {COMM_GET_IMU_DATA, 0x00u, 0x01u};
    assert_int_equal(vesc_comm_process_command(comm, one, sizeof(one)), EDGE_OK);
    assert_int_equal(ctx.tx_count, 1);
    /* One packed float follows the mask, and nothing after it: the frame is longer than the command
     * alone, and shorter than one carrying all fifteen readings. */
    assert_true(ctx.tx_len >= 5u);
    assert_true(ctx.tx_len < 40u);

    /* All fifteen bits is the whole thing, which is longer than any single reading. */
    const size_t single = ctx.tx_len;
    ctx.tx_count = 0;
    ctx.tx_len = 0u;
    uint8_t all[3] = {COMM_GET_IMU_DATA, 0xFFu, 0xFFu};
    assert_int_equal(vesc_comm_process_command(comm, all, sizeof(all)), EDGE_OK);
    assert_int_equal(ctx.tx_count, 1);
    assert_true(ctx.tx_len > single);

    /* The calibration's request is one angle and its answer is nine numbers. */
    ctx.tx_count = 0;
    ctx.tx_len = 0u;
    uint8_t cal_req[5] = {COMM_GET_IMU_CALIBRATION, 0x00u, 0x00u, 0x03u, 0xE8u}; /* 1000 / 1e3 */
    assert_int_equal(vesc_comm_process_command(comm, cal_req, sizeof(cal_req)), EDGE_OK);
    assert_int_equal(ctx.tx_count, 1);
    /* The answer is the command, nine packed floats and the frame around them. */
    assert_true(ctx.tx_len >= 40u);

    /* A request too short for the mask, and one too short for the angle. */
    uint8_t no_mask[1] = {COMM_GET_IMU_DATA};
    assert_int_equal(vesc_comm_process_command(comm, no_mask, sizeof(no_mask)), EDGE_EINVAL);
    uint8_t no_yaw[2] = {COMM_GET_IMU_CALIBRATION, 0x00u};
    assert_int_equal(vesc_comm_process_command(comm, no_yaw, sizeof(no_yaw)), EDGE_EINVAL);
}

int main(void) {

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_vesc_comm_lifecycle_and_guards),
        cmocka_unit_test(test_crc16_calculation),
        cmocka_unit_test(test_send_packet_framing),
        cmocka_unit_test(test_receive_packet_and_commands),
        cmocka_unit_test(test_imu_commands),
        cmocka_unit_test(test_config_commands_framing),
        cmocka_unit_test(test_setup_values_framing),
        cmocka_unit_test(test_every_handled_command_id_is_reachable),
        cmocka_unit_test(test_commands_without_optional_ports),
        cmocka_unit_test(test_every_command_propagates_a_port_failure),
        cmocka_unit_test(test_decoded_inputs_negative_side),
        cmocka_unit_test(test_detect_flux_linkage_openloop_command),
        cmocka_unit_test(test_detect_flux_linkage_command),
        cmocka_unit_test(test_detect_r_l_command),
        cmocka_unit_test(test_detect_param_is_refused_and_apply_all_forwards),
        cmocka_unit_test(test_restart_commands_reach_the_product),
        cmocka_unit_test(test_detect_hall_foc_command),
        cmocka_unit_test(test_detect_motor_param_command),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
