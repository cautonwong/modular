#ifndef APP_GAME_TWOS_H
#define APP_GAME_TWOS_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "game_twos/ports.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TWOS_GRID_ROWS 4
#define TWOS_GRID_COLS 4
#define TWOS_GRID_CELLS (TWOS_GRID_ROWS * TWOS_GRID_COLS)

typedef enum twos_direction {
    TWOS_DIR_LEFT = 0,
    TWOS_DIR_RIGHT = 1,
    TWOS_DIR_UP = 2,
    TWOS_DIR_DOWN = 3,
} twos_direction_t;

typedef struct twos_tile {
    uint16_t value;
    bool merged;
} twos_tile_t;

typedef struct game_twos_app {
    edge_module_t module;
    const twos_entropy_if_t *entropy_port;

    twos_tile_t grid[TWOS_GRID_ROWS][TWOS_GRID_COLS];
    uint32_t score;
    uint32_t high_score;
    bool game_over;
} game_twos_app_t;

void game_twos_construct(game_twos_app_t *self, uint32_t module_id, uint32_t priority,
                         const twos_entropy_if_t *entropy);

edge_status_t game_twos_init(game_twos_app_t *self);
edge_status_t game_twos_shutdown(game_twos_app_t *self);

void game_twos_reset(game_twos_app_t *self);
bool game_twos_swipe(game_twos_app_t *self, twos_direction_t dir);
uint32_t game_twos_get_score(const game_twos_app_t *self);
uint32_t game_twos_get_high_score(const game_twos_app_t *self);
uint16_t game_twos_get_cell(const game_twos_app_t *self, uint8_t row, uint8_t col);
bool game_twos_is_game_over(const game_twos_app_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_GAME_TWOS_H */
