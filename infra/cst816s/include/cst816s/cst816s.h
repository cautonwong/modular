#ifndef INFRA_CST816S_H
#define INFRA_CST816S_H

#include "edge/module.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CST816S_I2C_ADDR 0x15u

#define CST816S_REG_GESTURE_ID 0x01u
#define CST816S_REG_FINGER_NUM 0x02u
#define CST816S_REG_XPOS_H 0x03u
#define CST816S_REG_XPOS_L 0x04u
#define CST816S_REG_YPOS_H 0x05u
#define CST816S_REG_YPOS_L 0x06u
#define CST816S_REG_CHIP_ID 0xA7u
#define CST816S_REG_VENDOR_ID 0xA8u
#define CST816S_REG_FW_VERSION 0xA9u
#define CST816S_REG_SLEEP_MODE 0xFEu

typedef enum cst816s_gesture {
    CST816S_GESTURE_NONE = 0x00,
    CST816S_GESTURE_SLIDE_DOWN = 0x01,
    CST816S_GESTURE_SLIDE_UP = 0x02,
    CST816S_GESTURE_SLIDE_LEFT = 0x03,
    CST816S_GESTURE_SLIDE_RIGHT = 0x04,
    CST816S_GESTURE_SINGLE_TAP = 0x05,
    CST816S_GESTURE_DOUBLE_TAP = 0x0B,
    CST816S_GESTURE_LONG_PRESS = 0x0C,
} cst816s_gesture_t;

typedef struct cst816s_touch_info {
    uint16_t x;
    uint16_t y;
    bool touching;
    cst816s_gesture_t gesture;
    bool is_valid;
} cst816s_touch_info_t;

/* Parse 6-byte raw data read from CST816S starting at register 0x01 */
edge_status_t cst816s_parse_touch_data(const uint8_t *raw_6bytes, uint16_t max_x, uint16_t max_y,
                                       cst816s_touch_info_t *out_info);

/* Validate Chip ID and Vendor ID */
bool cst816s_validate_device_ids(uint8_t chip_id, uint8_t vendor_id);

/* Format deep sleep enter command */
edge_status_t cst816s_format_sleep_cmd(uint8_t out_cmd[2]);

#ifdef __cplusplus
}
#endif

#endif /* INFRA_CST816S_H */
