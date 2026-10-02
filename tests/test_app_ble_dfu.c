#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "ble_dfu/ble_dfu.h"
#include "edge/events.h"
#include "edge/modules.h"

static uint8_t s_mock_flash_mem[1024];
static uint8_t s_last_resp_opcode = 0;
static uint8_t s_last_resp_err = 0;
static uint32_t s_last_prn_bytes = 0;
static bool s_system_reset_called = false;

static edge_status_t mock_flash_erase(void *self, uint32_t offset, uint32_t length) {
    (void)self;
    (void)offset;
    (void)length;
    memset(s_mock_flash_mem, 0xFF, sizeof(s_mock_flash_mem));
    return EDGE_OK;
}

static edge_status_t mock_flash_write(void *self, uint32_t offset, const uint8_t *data,
                                      uint32_t size) {
    (void)self;
    uint32_t local_off = offset - BLE_DFU_FLASH_OFFSET;
    if (local_off + size <= sizeof(s_mock_flash_mem)) {
        memcpy(&s_mock_flash_mem[local_off], data, size);
    }
    return EDGE_OK;
}

static edge_status_t mock_flash_read(void *self, uint32_t offset, uint8_t *data, uint32_t size) {
    (void)self;
    uint32_t local_off = offset - BLE_DFU_FLASH_OFFSET;
    if (local_off + size <= sizeof(s_mock_flash_mem)) {
        memcpy(data, &s_mock_flash_mem[local_off], size);
    }
    return EDGE_OK;
}

static edge_status_t mock_notify_response(void *self, uint8_t req_opcode, uint8_t error_code) {
    (void)self;
    s_last_resp_opcode = req_opcode;
    s_last_resp_err = error_code;
    return EDGE_OK;
}

static edge_status_t mock_notify_prn(void *self, uint32_t bytes_received) {
    (void)self;
    s_last_prn_bytes = bytes_received;
    return EDGE_OK;
}

static edge_status_t mock_system_reset(void *self) {
    (void)self;
    s_system_reset_called = true;
    return EDGE_OK;
}

static void test_ble_dfu_flow(void **state) {
    (void)state;

    ble_dfu_flash_port_t flash = {
        .self = NULL,
        .erase_range = mock_flash_erase,
        .write_chunk = mock_flash_write,
        .read_chunk = mock_flash_read,
    };
    ble_dfu_notify_port_t notify = {
        .self = NULL,
        .send_response = mock_notify_response,
        .send_prn = mock_notify_prn,
    };
    ble_dfu_system_port_t sys = {
        .self = NULL,
        .system_reset = mock_system_reset,
    };

    edge_event_t event_storage[8];
    edge_event_queue_t queue;
    assert_int_equal(edge_event_queue_init(&queue, event_storage, 8u), EDGE_OK);

    edge_event_sink_t sink = {
        .queue = &queue,
        .clock = NULL,
        .guard = NULL,
    };

    ble_dfu_t dfu;
    ble_dfu_init(&dfu, &flash, &notify, &sys, &sink);

    /* 1. StartDFU for application image of 16 bytes */
    uint8_t start_dfu[6] = {0x01, 0x04, 16, 0, 0, 0};
    assert_int_equal(ble_dfu_control_point_handler(&dfu, start_dfu, sizeof(start_dfu)), EDGE_OK);
    assert_int_equal(s_last_resp_opcode, BLE_DFU_OPCODE_START_DFU);
    assert_int_equal(s_last_resp_err, BLE_DFU_ERR_NO_ERROR);
    assert_int_equal(ble_dfu_get_state(&dfu), BLE_DFU_STATE_START);

    /* Compute expected CRC for 16-byte dummy firmware */
    uint8_t dummy_fw[16] = "12345678abcdefgh";
    uint16_t expected_crc = ble_dfu_compute_crc16(dummy_fw, 16, 0xFFFFu);

    /* 2. InitDFUParams with expected CRC */
    uint8_t init_params[5] = {0x02, 16, 0, (uint8_t)(expected_crc & 0xFFu),
                              (uint8_t)((expected_crc >> 8u) & 0xFFu)};
    assert_int_equal(ble_dfu_control_point_handler(&dfu, init_params, sizeof(init_params)),
                     EDGE_OK);
    assert_int_equal(s_last_resp_opcode, BLE_DFU_OPCODE_INIT_DFU_PARAMS);
    assert_int_equal(s_last_resp_err, BLE_DFU_ERR_NO_ERROR);

    /* 3. ReceiveFirmwareImage opcode */
    uint8_t rcv_cmd[1] = {0x03};
    assert_int_equal(ble_dfu_control_point_handler(&dfu, rcv_cmd, sizeof(rcv_cmd)), EDGE_OK);
    assert_int_equal(ble_dfu_get_state(&dfu), BLE_DFU_STATE_DATA);

    /* 4. Stream data packets */
    assert_int_equal(ble_dfu_write_packet_handler(&dfu, &dummy_fw[0], 8), EDGE_OK);
    assert_int_equal(ble_dfu_write_packet_handler(&dfu, &dummy_fw[8], 8), EDGE_OK);

    /* 5. ValidateFirmware */
    uint8_t val_cmd[1] = {0x04};
    assert_int_equal(ble_dfu_control_point_handler(&dfu, val_cmd, sizeof(val_cmd)), EDGE_OK);
    assert_int_equal(s_last_resp_opcode, BLE_DFU_OPCODE_VALIDATE_FW);
    assert_int_equal(s_last_resp_err, BLE_DFU_ERR_NO_ERROR);
    assert_int_equal(ble_dfu_get_state(&dfu), BLE_DFU_STATE_VALIDATED);

    /* 6. ActivateImageAndReset */
    s_system_reset_called = false;
    uint8_t act_cmd[1] = {0x05};
    assert_int_equal(ble_dfu_control_point_handler(&dfu, act_cmd, sizeof(act_cmd)), EDGE_OK);
    assert_true(s_system_reset_called);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ble_dfu_flow),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
