#include "cst816s/cst816s.h"

edge_status_t cst816s_parse_touch_data(const uint8_t *raw_6bytes, uint16_t max_x, uint16_t max_y,
                                       cst816s_touch_info_t *out_info) {
    if (raw_6bytes == NULL || out_info == NULL) {
        return EDGE_EINVAL;
    }

    uint8_t gesture_raw = raw_6bytes[0];
    uint8_t points = raw_6bytes[1];
    uint16_t x = (uint16_t)(((uint16_t)(raw_6bytes[2] & 0x0Fu) << 8u) | raw_6bytes[3]);
    uint16_t y = (uint16_t)(((uint16_t)(raw_6bytes[4] & 0x0Fu) << 8u) | raw_6bytes[5]);

    if (x > max_x || y > max_y) {
        out_info->is_valid = false;
        return EDGE_EINVAL;
    }

    out_info->x = x;
    out_info->y = y;
    out_info->touching = (points > 0u);
    out_info->is_valid = true;

    switch (gesture_raw) {
    case 0x01:
        out_info->gesture = CST816S_GESTURE_SLIDE_DOWN;
        break;
    case 0x02:
        out_info->gesture = CST816S_GESTURE_SLIDE_UP;
        break;
    case 0x03:
        out_info->gesture = CST816S_GESTURE_SLIDE_LEFT;
        break;
    case 0x04:
        out_info->gesture = CST816S_GESTURE_SLIDE_RIGHT;
        break;
    case 0x05:
        out_info->gesture = CST816S_GESTURE_SINGLE_TAP;
        break;
    case 0x0B:
        out_info->gesture = CST816S_GESTURE_DOUBLE_TAP;
        break;
    case 0x0C:
        out_info->gesture = CST816S_GESTURE_LONG_PRESS;
        break;
    default:
        out_info->gesture = CST816S_GESTURE_NONE;
        break;
    }

    return EDGE_OK;
}

bool cst816s_validate_device_ids(uint8_t chip_id, uint8_t vendor_id) {
    (void)vendor_id;
    return (chip_id == 0xB4u || chip_id == 0xB5u || chip_id == 0xB6u);
}

edge_status_t cst816s_format_sleep_cmd(uint8_t out_cmd[2]) {
    if (out_cmd == NULL) {
        return EDGE_EINVAL;
    }
    out_cmd[0] = CST816S_REG_SLEEP_MODE;
    out_cmd[1] = 0x03u; /* Sleep mode enable */
    return EDGE_OK;
}
