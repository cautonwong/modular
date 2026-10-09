#include "st7789/st7789.h"

edge_status_t st7789_format_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                                   st7789_window_cmds_t *out_cmds) {
    if (out_cmds == NULL || x0 >= ST7789_WIDTH || x1 >= ST7789_WIDTH || x0 > x1 ||
        y0 >= ST7789_HEIGHT || y1 >= ST7789_HEIGHT || y0 > y1) {
        return EDGE_EINVAL;
    }

    out_cmds->caset_cmd = ST7789_CMD_CASET;
    out_cmds->caset_data[0] = (uint8_t)(x0 >> 8u);
    out_cmds->caset_data[1] = (uint8_t)(x0 & 0xFFu);
    out_cmds->caset_data[2] = (uint8_t)(x1 >> 8u);
    out_cmds->caset_data[3] = (uint8_t)(x1 & 0xFFu);

    out_cmds->raset_cmd = ST7789_CMD_RASET;
    out_cmds->raset_data[0] = (uint8_t)(y0 >> 8u);
    out_cmds->raset_data[1] = (uint8_t)(y0 & 0xFFu);
    out_cmds->raset_data[2] = (uint8_t)(y1 >> 8u);
    out_cmds->raset_data[3] = (uint8_t)(y1 & 0xFFu);

    out_cmds->ramwr_cmd = ST7789_CMD_RAMWR;

    return EDGE_OK;
}

edge_status_t st7789_format_scroll(uint16_t line, st7789_scroll_cmd_t *out_cmd) {
    if (out_cmd == NULL || line >= 320u) {
        return EDGE_EINVAL;
    }
    out_cmd->cmd = ST7789_CMD_VSCRSADD;
    out_cmd->data[0] = (uint8_t)(line >> 8u);
    out_cmd->data[1] = (uint8_t)(line & 0xFFu);
    return EDGE_OK;
}
