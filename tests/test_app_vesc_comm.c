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
#include "vesc_comm/vesc_comm.h"

typedef struct mock_comm_ctx {
    uint8_t tx_buf[1024];
    size_t tx_len;
    size_t tx_count;

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
    vesc_comm_construct(NULL, EDGE_MOD_VESC_COMM, 10u, NULL, NULL, NULL, NULL, NULL);

    mock_comm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    edge_stream_tx_port_t tx_port = {.write = mock_stream_write, .self = &ctx};

    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, NULL, &test_identity);
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
    vesc_comm_construct(no_tx, EDGE_MOD_VESC_COMM, 10u, NULL, NULL, NULL, NULL, &test_identity);
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
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10, &tx_port, NULL, NULL, NULL, &test_identity);
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
                        &test_identity);
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
    vesc_comm_construct(oversize_comm, EDGE_MOD_VESC_COMM, 10, &tx_port, NULL, NULL, NULL,
                        &oversized_identity);
    assert_int_equal(vesc_comm_init(oversize_comm), EDGE_OK);
    ctx.tx_count = 0;
    assert_int_equal(vesc_comm_process_command(oversize_comm, cmd_fw, sizeof(cmd_fw)), EDGE_EINVAL);
    assert_int_equal(ctx.tx_count, 0);

    /* A codec with no identity at all refuses the same way. */
    vesc_comm_construct(oversize_comm, EDGE_MOD_VESC_COMM, 10, &tx_port, NULL, NULL, NULL, NULL);
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
        .self = &cfg,
    };

    vesc_comm_t *comm = test_comm_alloc();
    vesc_comm_construct(comm, EDGE_MOD_VESC_COMM, 10u, &tx_port, NULL, NULL, &config_port,
                        &test_identity);
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
    assert_int_equal(vesc_comm_process_command(comm, set_mc, sizeof(set_mc)), EDGE_OK);
    assert_int_equal(cfg.set_mc_len, 4u);
    assert_int_equal(cfg.set_mc[0], 0xBCu);
    assert_int_equal(cfg.set_mc[3], 0xB0u);
}
int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_vesc_comm_lifecycle_and_guards),
        cmocka_unit_test(test_crc16_calculation),
        cmocka_unit_test(test_send_packet_framing),
        cmocka_unit_test(test_receive_packet_and_commands),
        cmocka_unit_test(test_config_commands_framing),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
