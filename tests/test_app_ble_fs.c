#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "ble_fs/ble_fs.h"
#include "edge/events.h"
#include "edge/modules.h"

static uint8_t s_mock_file_storage[1024];
static uint32_t s_mock_file_len = 0;
static uint8_t s_last_resp_data[256];
static size_t s_last_resp_len = 0;

static edge_status_t mock_fs_open(void *self, const char *path, uint32_t flags) {
    (void)self;
    (void)path;
    (void)flags;
    return EDGE_OK;
}

static edge_status_t mock_fs_close(void *self) {
    (void)self;
    return EDGE_OK;
}

static edge_status_t mock_fs_read(void *self, uint32_t offset, uint8_t *buf, uint32_t size,
                                  uint32_t *read_len) {
    (void)self;
    if (offset >= s_mock_file_len) {
        *read_len = 0;
        return EDGE_OK;
    }
    uint32_t avail = s_mock_file_len - offset;
    uint32_t to_read = (size < avail) ? size : avail;
    memcpy(buf, &s_mock_file_storage[offset], to_read);
    *read_len = to_read;
    return EDGE_OK;
}

static edge_status_t mock_fs_write(void *self, uint32_t offset, const uint8_t *buf, uint32_t size) {
    (void)self;
    if (offset + size > sizeof(s_mock_file_storage)) {
        return EDGE_ENOSPC;
    }
    memcpy(&s_mock_file_storage[offset], buf, size);
    if (offset + size > s_mock_file_len) {
        s_mock_file_len = offset + size;
    }
    return EDGE_OK;
}

static edge_status_t mock_fs_delete(void *self, const char *path) {
    (void)self;
    (void)path;
    s_mock_file_len = 0;
    return EDGE_OK;
}

static edge_status_t mock_fs_mkdir(void *self, const char *path) {
    (void)self;
    (void)path;
    return EDGE_OK;
}

static edge_status_t mock_fs_rename(void *self, const char *old_path, const char *new_path) {
    (void)self;
    (void)old_path;
    (void)new_path;
    return EDGE_OK;
}

static edge_status_t mock_fs_get_free(void *self, uint32_t *free_bytes) {
    (void)self;
    *free_bytes = 1048576u;
    return EDGE_OK;
}

static edge_status_t mock_tx_send(void *self, const uint8_t *data, size_t len) {
    (void)self;
    if (len <= sizeof(s_last_resp_data)) {
        memcpy(s_last_resp_data, data, len);
        s_last_resp_len = len;
    }
    return EDGE_OK;
}

static void test_ble_fs_read_write_delete(void **state) {
    (void)state;

    ble_fs_storage_port_t storage = {
        .self = NULL,
        .file_open = mock_fs_open,
        .file_close = mock_fs_close,
        .file_read = mock_fs_read,
        .file_write = mock_fs_write,
        .file_delete = mock_fs_delete,
        .dir_create = mock_fs_mkdir,
        .file_rename = mock_fs_rename,
        .get_free_space = mock_fs_get_free,
    };

    ble_fs_tx_port_t tx = {
        .self = NULL,
        .send_response = mock_tx_send,
    };

    edge_event_t event_storage[8];
    edge_event_queue_t queue;
    assert_int_equal(edge_event_queue_init(&queue, event_storage, 8u), EDGE_OK);

    edge_event_sink_t sink = {
        .queue = &queue,
        .clock = NULL,
        .guard = NULL,
    };

    ble_fs_t fs;
    ble_fs_init(&fs, &storage, &tx, &sink);

    /* Write packet: cmd=0x20, pad=0, pathlen=8, offset=0, modTime=0, totalsize=12, path="test.txt"
     */
    uint8_t write_req[28] = {0};
    write_req[0] = BLE_FS_CMD_WRITE;
    write_req[2] = 8;   /* pathlen */
    write_req[16] = 12; /* totalsize */
    memcpy(&write_req[20], "test.txt", 8);

    assert_int_equal(ble_fs_process_packet(&fs, write_req, sizeof(write_req)), EDGE_OK);
    assert_int_equal(s_last_resp_data[0], BLE_FS_CMD_WRITE_PACING);
    assert_int_equal(s_last_resp_data[1], BLE_FS_STATUS_OK);

    /* Stream data: cmd=0x22, status=0, pad=0, offset=0, datasize=12, data="Hello FS 123" */
    uint8_t write_data[24] = {0};
    write_data[0] = BLE_FS_CMD_WRITE_DATA;
    write_data[8] = 12; /* datasize */
    memcpy(&write_data[12], "Hello FS 123", 12);

    assert_int_equal(ble_fs_process_packet(&fs, write_data, sizeof(write_data)), EDGE_OK);
    assert_int_equal(s_mock_file_len, 12);

    /* Read packet: cmd=0x10, pad=0, pathlen=8, chunkoff=0, chunksize=12, path="test.txt" */
    uint8_t read_req[20] = {0};
    read_req[0] = BLE_FS_CMD_READ;
    read_req[2] = 8;
    read_req[8] = 12;
    memcpy(&read_req[12], "test.txt", 8);

    assert_int_equal(ble_fs_process_packet(&fs, read_req, sizeof(read_req)), EDGE_OK);
    assert_int_equal(s_last_resp_data[0], BLE_FS_CMD_READ_DATA);
    assert_int_equal(s_last_resp_data[1], BLE_FS_STATUS_OK);
    assert_int_equal(s_last_resp_data[12], 12); /* chunklen */
    assert_memory_equal(&s_last_resp_data[16], "Hello FS 123", 12);

    /* Delete packet */
    uint8_t del_req[12] = {0};
    del_req[0] = BLE_FS_CMD_DELETE;
    del_req[2] = 8;
    memcpy(&del_req[4], "test.txt", 8);

    assert_int_equal(ble_fs_process_packet(&fs, del_req, sizeof(del_req)), EDGE_OK);
    assert_int_equal(s_last_resp_data[0], BLE_FS_CMD_DELETE_STATUS);
    assert_int_equal(s_last_resp_data[1], BLE_FS_STATUS_OK);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ble_fs_read_write_delete),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
