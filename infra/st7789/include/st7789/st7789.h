#ifndef INFRA_ST7789_H
#define INFRA_ST7789_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ST7789_WIDTH 240u
#define ST7789_HEIGHT 240u

#define ST7789_CMD_NOP 0x00u
#define ST7789_CMD_SWRESET 0x01u
#define ST7789_CMD_SLPIN 0x10u
#define ST7789_CMD_SLPOUT 0x11u
#define ST7789_CMD_NORON 0x13u
#define ST7789_CMD_INVOFF 0x20u
#define ST7789_CMD_INVON 0x21u
#define ST7789_CMD_DISPOFF 0x28u
#define ST7789_CMD_DISPON 0x29u
#define ST7789_CMD_CASET 0x2Au
#define ST7789_CMD_RASET 0x2Bu
#define ST7789_CMD_RAMWR 0x2Cu
#define ST7789_CMD_VSCRDEF 0x33u
#define ST7789_CMD_MADCTL 0x36u
#define ST7789_CMD_VSCRSADD 0x37u
#define ST7789_CMD_IDMOFF 0x38u
#define ST7789_CMD_IDMON 0x39u
#define ST7789_CMD_COLMOD 0x3Au
#define ST7789_CMD_PORCTRL 0xB2u
#define ST7789_CMD_GCTRL 0xB7u
#define ST7789_CMD_VCOMS 0xBBu
#define ST7789_CMD_LCMCTRL 0xC0u
#define ST7789_CMD_VDVVRHEN 0xC2u
#define ST7789_CMD_VRHS 0xC3u
#define ST7789_CMD_VDVS 0xC4u
#define ST7789_CMD_FRCTRL2 0xC6u
#define ST7789_CMD_FRCTRL1 0xB3u
#define ST7789_CMD_PWCTRL1 0xD0u
#define ST7789_CMD_PWCTRL2 0xE8u
#define ST7789_CMD_CMD2EN 0xDFu

typedef struct st7789_window_cmds {
    uint8_t caset_cmd;
    uint8_t caset_data[4];
    uint8_t raset_cmd;
    uint8_t raset_data[4];
    uint8_t ramwr_cmd;
} st7789_window_cmds_t;

typedef struct st7789_scroll_cmd {
    uint8_t cmd;
    uint8_t data[2];
} st7789_scroll_cmd_t;

/**
 * Format ST7789 column/row address set command buffers for a given bounding box.
 */
edge_status_t st7789_format_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                                   st7789_window_cmds_t *out_cmds);

/**
 * Format ST7789 vertical scrolling start address command.
 */
edge_status_t st7789_format_scroll(uint16_t line, st7789_scroll_cmd_t *out_cmd);

#ifdef __cplusplus
}
#endif

#endif /* INFRA_ST7789_H */
