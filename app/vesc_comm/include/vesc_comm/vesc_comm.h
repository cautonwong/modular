#ifndef APP_VESC_COMM_H
#define APP_VESC_COMM_H

#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VESC_PACKET_MAX_PL_LEN 512
#define VESC_PACKET_BUF_LEN (VESC_PACKET_MAX_PL_LEN + 8)

/*
 * Scratch for building one command reply. Owned by the caller inside vesc_comm_t
 * rather than living on the handler's stack: the reference pools this memory
 * (util/mempools.c mempools_get_packet_buffer) for the same reason, and this
 * architecture's rule is that buffers are provided by the composition root, never
 * allocated and never hidden in a deep frame. The largest reply the reference
 * defines is COMM_GET_VALUES at 74 bytes.
 */
#define VESC_CMD_REPLY_BUF_LEN 128u

/* COMM_GET_VALUES is the largest reply the reference defines (74 bytes); keep the
 * scratch above it so the send-path bound is a backstop rather than the normal
 * case, and so a field added later trips the compiler instead of the wire. */
_Static_assert(VESC_CMD_REPLY_BUF_LEN >= 96u, "reply scratch too small for GET_VALUES");

/*
 * The whole COMM_PACKET_ID table, copied from the reference's datatypes.h so the
 * wire ids cannot drift. Declaring the full table is load-bearing: a hand-picked
 * subset previously carried three wrong values (DECODED_ADC, DECODED_PPM,
 * GET_VALUES_SETUP) because the offsets were guessed. Only the ids listed in
 * vesc_comm_process_command are handled; the rest are the id space this port has
 * not reached yet.
 */
typedef enum {
    COMM_FW_VERSION = 0,
    COMM_JUMP_TO_BOOTLOADER = 1,
    COMM_ERASE_NEW_APP = 2,
    COMM_WRITE_NEW_APP_DATA = 3,
    COMM_GET_VALUES = 4,
    COMM_SET_DUTY = 5,
    COMM_SET_CURRENT = 6,
    COMM_SET_CURRENT_BRAKE = 7,
    COMM_SET_RPM = 8,
    COMM_SET_POS = 9,
    COMM_SET_HANDBRAKE = 10,
    COMM_SET_DETECT = 11,
    COMM_SET_SERVO_POS = 12,
    COMM_SET_MCCONF = 13,
    COMM_GET_MCCONF = 14,
    COMM_GET_MCCONF_DEFAULT = 15,
    COMM_SET_APPCONF = 16,
    COMM_GET_APPCONF = 17,
    COMM_GET_APPCONF_DEFAULT = 18,
    COMM_SAMPLE_PRINT = 19,
    COMM_TERMINAL_CMD = 20,
    COMM_PRINT = 21,
    COMM_ROTOR_POSITION = 22,
    COMM_EXPERIMENT_SAMPLE = 23,
    COMM_DETECT_MOTOR_PARAM = 24,
    COMM_DETECT_MOTOR_R_L = 25,
    COMM_DETECT_MOTOR_FLUX_LINKAGE = 26,
    COMM_DETECT_ENCODER = 27,
    COMM_DETECT_HALL_FOC = 28,
    COMM_REBOOT = 29,
    COMM_ALIVE = 30,
    COMM_GET_DECODED_PPM = 31,
    COMM_GET_DECODED_ADC = 32,
    COMM_GET_DECODED_CHUK = 33,
    COMM_FORWARD_CAN = 34,
    COMM_SET_CHUCK_DATA = 35,
    COMM_CUSTOM_APP_DATA = 36,
    COMM_NRF_START_PAIRING = 37,
    COMM_GPD_SET_FSW = 38,
    COMM_GPD_BUFFER_NOTIFY = 39,
    COMM_GPD_BUFFER_SIZE_LEFT = 40,
    COMM_GPD_FILL_BUFFER = 41,
    COMM_GPD_OUTPUT_SAMPLE = 42,
    COMM_GPD_SET_MODE = 43,
    COMM_GPD_FILL_BUFFER_INT8 = 44,
    COMM_GPD_FILL_BUFFER_INT16 = 45,
    COMM_GPD_SET_BUFFER_INT_SCALE = 46,
    COMM_GET_VALUES_SETUP = 47,
    COMM_SET_MCCONF_TEMP = 48,
    COMM_SET_MCCONF_TEMP_SETUP = 49,
    COMM_GET_VALUES_SELECTIVE = 50,
    COMM_GET_VALUES_SETUP_SELECTIVE = 51,
    COMM_EXT_NRF_PRESENT = 52,
    COMM_EXT_NRF_ESB_SET_CH_ADDR = 53,
    COMM_EXT_NRF_ESB_SEND_DATA = 54,
    COMM_EXT_NRF_ESB_RX_DATA = 55,
    COMM_EXT_NRF_SET_ENABLED = 56,
    COMM_DETECT_MOTOR_FLUX_LINKAGE_OPENLOOP = 57,
    COMM_DETECT_APPLY_ALL_FOC = 58,
    COMM_JUMP_TO_BOOTLOADER_ALL_CAN = 59,
    COMM_ERASE_NEW_APP_ALL_CAN = 60,
    COMM_WRITE_NEW_APP_DATA_ALL_CAN = 61,
    COMM_PING_CAN = 62,
    COMM_APP_DISABLE_OUTPUT = 63,
    COMM_TERMINAL_CMD_SYNC = 64,
    COMM_GET_IMU_DATA = 65,
    COMM_BM_CONNECT = 66,
    COMM_BM_ERASE_FLASH_ALL = 67,
    COMM_BM_WRITE_FLASH = 68,
    COMM_BM_REBOOT = 69,
    COMM_BM_DISCONNECT = 70,
    COMM_BM_MAP_PINS_DEFAULT = 71,
    COMM_BM_MAP_PINS_NRF5X = 72,
    COMM_ERASE_BOOTLOADER = 73,
    COMM_ERASE_BOOTLOADER_ALL_CAN = 74,
    COMM_PLOT_INIT = 75,
    COMM_PLOT_DATA = 76,
    COMM_PLOT_ADD_GRAPH = 77,
    COMM_PLOT_SET_GRAPH = 78,
    COMM_GET_DECODED_BALANCE = 79,
    COMM_BM_MEM_READ = 80,
    COMM_WRITE_NEW_APP_DATA_LZO = 81,
    COMM_WRITE_NEW_APP_DATA_ALL_CAN_LZO = 82,
    COMM_BM_WRITE_FLASH_LZO = 83,
    COMM_SET_CURRENT_REL = 84,
    COMM_CAN_FWD_FRAME = 85,
    COMM_SET_BATTERY_CUT = 86,
    COMM_SET_BLE_NAME = 87,
    COMM_SET_BLE_PIN = 88,
    COMM_SET_CAN_MODE = 89,
    COMM_GET_IMU_CALIBRATION = 90,
    COMM_GET_MCCONF_TEMP = 91,
    COMM_GET_CUSTOM_CONFIG_XML = 92,
    COMM_GET_CUSTOM_CONFIG = 93,
    COMM_GET_CUSTOM_CONFIG_DEFAULT = 94,
    COMM_SET_CUSTOM_CONFIG = 95,
    COMM_BMS_GET_VALUES = 96,
    COMM_BMS_SET_CHARGE_ALLOWED = 97,
    COMM_BMS_SET_BALANCE_OVERRIDE = 98,
    COMM_BMS_RESET_COUNTERS = 99,
    COMM_BMS_FORCE_BALANCE = 100,
    COMM_BMS_ZERO_CURRENT_OFFSET = 101,
    COMM_JUMP_TO_BOOTLOADER_HW = 102,
    COMM_ERASE_NEW_APP_HW = 103,
    COMM_WRITE_NEW_APP_DATA_HW = 104,
    COMM_ERASE_BOOTLOADER_HW = 105,
    COMM_JUMP_TO_BOOTLOADER_ALL_CAN_HW = 106,
    COMM_ERASE_NEW_APP_ALL_CAN_HW = 107,
    COMM_WRITE_NEW_APP_DATA_ALL_CAN_HW = 108,
    COMM_ERASE_BOOTLOADER_ALL_CAN_HW = 109,
    COMM_SET_ODOMETER = 110,
    COMM_PSW_GET_STATUS = 111,
    COMM_PSW_SWITCH = 112,
    COMM_BMS_FWD_CAN_RX = 113,
    COMM_BMS_HW_DATA = 114,
    COMM_GET_BATTERY_CUT = 115,
    COMM_BM_HALT_REQ = 116,
    COMM_GET_QML_UI_HW = 117,
    COMM_GET_QML_UI_APP = 118,
    COMM_CUSTOM_HW_DATA = 119,
    COMM_QMLUI_ERASE = 120,
    COMM_QMLUI_WRITE = 121,
    COMM_IO_BOARD_GET_ALL = 122,
    COMM_IO_BOARD_SET_PWM = 123,
    COMM_IO_BOARD_SET_DIGITAL = 124,
    COMM_BM_MEM_WRITE = 125,
    COMM_BMS_BLNC_SELFTEST = 126,
    COMM_GET_EXT_HUM_TMP = 127,
    COMM_GET_STATS = 128,
    COMM_RESET_STATS = 129,
    COMM_LISP_READ_CODE = 130,
    COMM_LISP_WRITE_CODE = 131,
    COMM_LISP_ERASE_CODE = 132,
    COMM_LISP_SET_RUNNING = 133,
    COMM_LISP_GET_STATS = 134,
    COMM_LISP_PRINT = 135,
    COMM_BMS_SET_BATT_TYPE = 136,
    COMM_BMS_GET_BATT_TYPE = 137,
    COMM_LISP_REPL_CMD = 138,
    COMM_LISP_STREAM_CODE = 139,
    COMM_FILE_LIST = 140,
    COMM_FILE_READ = 141,
    COMM_FILE_WRITE = 142,
    COMM_FILE_MKDIR = 143,
    COMM_FILE_REMOVE = 144,
    COMM_LOG_START = 145,
    COMM_LOG_STOP = 146,
    COMM_LOG_CONFIG_FIELD = 147,
    COMM_LOG_DATA_F32 = 148,
    COMM_SET_APPCONF_NO_STORE = 149,
    COMM_GET_GNSS = 150,
    COMM_LOG_DATA_F64 = 151,
    COMM_LISP_RMSG = 152,
    COMM_PINLOCK1 = 153,
    COMM_PINLOCK2 = 154,
    COMM_PINLOCK3 = 155,
    COMM_SHUTDOWN = 156,
    COMM_FW_INFO = 157,
    COMM_CAN_UPDATE_BAUD_ALL = 158,
    COMM_MOTOR_ESTOP = 159,
} vesc_comm_cmd_t;

/*
 * Telemetry snapshot for COMM_GET_VALUES / COMM_GET_VALUES_SELECTIVE. The mask
 * selects which fields the caller will send (reference: comm/commands.c, the
 * `mask & (1 << n)` ladder); it is also what decides which read-and-reset
 * averages the provider may consume. Fields are the reference's.
 */
typedef struct vesc_values {
    float temp_mos;
    float temp_motor;
    float current_motor;
    float current_in;
    float id;
    float iq;
    float duty_now;
    float rpm;
    float v_in;
    float amp_hours;
    float amp_hours_charged;
    float watt_hours;
    float watt_hours_charged;
    int32_t tachometer;
    int32_t tachometer_abs;
    uint32_t fault_code;
    float pid_pos_now;
    uint8_t controller_id;
    float temp_mos_1;
    float temp_mos_2;
    float temp_mos_3;
    float vd;
    float vq;
    uint8_t status; /* bit 0 timeout, bit 1 kill switch (reference: bits 21) */
} vesc_values_t;

/* COMM_GET_VALUES asks for every field. */
#define VESC_VALUES_MASK_ALL 0xFFFFFFFFu

/*
 * COMM_GET_STATS payload. The request carries a uint16 mask and the reply echoes
 * it as a uint32 (reference: comm/commands.c COMM_GET_STATS), the averages are
 * sum/samples and the maxima are running maxima since the last reset.
 */
typedef struct vesc_stats {
    float speed_avg;
    float speed_max;
    float power_avg;
    float power_max;
    float current_avg;
    float current_max;
    float temp_mos_avg;
    float temp_mos_max;
    float temp_motor_avg;
    float temp_motor_max;
    float count_time;
} vesc_stats_t;

/*
 * Consumer-Defined Ports (Rules: must have void *self; callbacks take void *self)
 */
typedef struct edge_stream_tx_port {
    edge_status_t (*write)(void *self, const uint8_t *data, size_t len);
    void *self;
} edge_stream_tx_port_t;

/*
 * Decoded app inputs, for COMM_GET_DECODED_PPM and COMM_GET_DECODED_ADC. The
 * reference reads these straight off app_ppm / app_adc (comm/commands.c), so the
 * codec asks for the same quantities: the decoded normalised level and the raw
 * input behind it.
 */
typedef struct vesc_app_status_port {
    edge_status_t (*get_decoded_ppm)(void *self, float *level, float *pulse_us);
    edge_status_t (*get_decoded_adc)(void *self, float *level, float *voltage, float *level2,
                                     float *voltage2);
    void *self;
} vesc_app_status_port_t;

typedef struct vesc_motor_provider_port {
    /*
     * Fill out_val for the fields `mask` selects. The fields the reference serves
     * through mc_interface_read_reset_avg_* are averages since the previous call,
     * and reading them RESETS the accumulator - so a provider must read-and-reset
     * exactly the masked channels and leave the others untouched, or a selective
     * read would silently shorten every other average's window.
     */
    edge_status_t (*get_values)(void *self, uint32_t mask, vesc_values_t *out_val);
    edge_status_t (*set_duty)(void *self, float duty);
    edge_status_t (*set_current)(void *self, float current);
    edge_status_t (*set_current_brake)(void *self, float current);
    edge_status_t (*set_rpm)(void *self, float rpm);
    edge_status_t (*set_pos)(void *self, float pos);
    edge_status_t (*get_stats)(void *self, vesc_stats_t *out_val);
    edge_status_t (*reset_stats)(void *self);
    void *self;
} vesc_motor_provider_port_t;

/*
 * Product identity reported by COMM_FW_VERSION. These are facts of the build and
 * the board, so the composition root supplies them; keeping them out of the codec
 * is what stops a plausible-looking placeholder from going out on the wire. The
 * field set and order are the reference's (comm/commands.c COMM_FW_VERSION).
 */
typedef struct vesc_identity {
    const char *hw_name; /* reference: HW_NAME, hwconf/<board> */
    const char *fw_name; /* reference: FW_NAME */
    const uint8_t *uuid; /* 12 bytes, reference: STM32_UUID_8 */
    uint8_t fw_version_major;
    uint8_t fw_version_minor;
    uint8_t pairing_done;
    uint8_t fw_test_version;
    uint8_t hw_type; /* HW_TYPE_VESC = 0 */
    uint8_t custom_cfg_num;
    uint8_t phase_filters;
    uint8_t qmlui_hw; /* 0 none, 1, 2 fullscreen */
    uint8_t qmlui_app;
    uint8_t nrf_flags;
    uint8_t controller_id;
    uint32_t hw_crc; /* reference: main_calc_hw_crc() */
} vesc_identity_t;

typedef struct vesc_comm {
    edge_module_t module;

    /* Injected Ports */
    const edge_stream_tx_port_t *stream_tx;
    const vesc_motor_provider_port_t *motor;
    const vesc_app_status_port_t *app_status;
    const vesc_identity_t *identity;

    /* Packet RX State */
    uint8_t rx_buffer[VESC_PACKET_BUF_LEN];
    size_t rx_write_ptr;
    size_t rx_read_ptr;
    int bytes_left;

    /* Packet TX Buffer */
    uint8_t tx_buffer[VESC_PACKET_BUF_LEN];

    /* Caller-provided reply scratch; see VESC_CMD_REPLY_BUF_LEN. */
    uint8_t cmd_reply_buf[VESC_CMD_REPLY_BUF_LEN];

    /* Statistics */
    uint32_t packets_received;
    uint32_t packets_sent;
    uint32_t crc_errors;
} vesc_comm_t;

void vesc_comm_construct(vesc_comm_t *self, uint32_t module_id, uint32_t priority,
                         const edge_stream_tx_port_t *stream_tx,
                         const vesc_motor_provider_port_t *motor,
                         const vesc_app_status_port_t *app_status, const vesc_identity_t *identity);

edge_status_t vesc_comm_init(vesc_comm_t *self);
edge_status_t vesc_comm_deinit(vesc_comm_t *self);
edge_module_t *vesc_comm_module(vesc_comm_t *self);

/* Packet Processing & Framing */
void vesc_comm_process_byte(vesc_comm_t *self, uint8_t byte);
edge_status_t vesc_comm_send_packet(vesc_comm_t *self, const uint8_t *payload, size_t len);
edge_status_t vesc_comm_process_command(vesc_comm_t *self, const uint8_t *data, size_t len);

/* Helper: CRC16 calculation */
uint16_t vesc_crc16(const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* APP_VESC_COMM_H */
