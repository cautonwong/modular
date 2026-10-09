#ifndef APP_GAME_PADDLE_H
#define APP_GAME_PADDLE_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "game_paddle/ports.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PADDLE_SCREEN_WIDTH 240
#define PADDLE_SCREEN_HEIGHT 240
#define PADDLE_BALL_SIZE 16
#define PADDLE_HEIGHT 60
#define PADDLE_WIDTH 4

typedef struct game_paddle_app {
    edge_module_t module;
    const paddle_motor_if_t *motor_port;
    const paddle_entropy_if_t *entropy_port;

    int16_t ball_x;
    int16_t ball_y;
    int8_t dx;
    int8_t dy;
    uint16_t paddle_y;
    uint16_t score;
    uint16_t high_score;
    bool is_running;
} game_paddle_app_t;

void game_paddle_construct(game_paddle_app_t *self, uint32_t module_id, uint32_t priority,
                           const paddle_motor_if_t *motor, const paddle_entropy_if_t *entropy);

edge_status_t game_paddle_init(game_paddle_app_t *self);
edge_status_t game_paddle_shutdown(game_paddle_app_t *self);

void game_paddle_reset(game_paddle_app_t *self);
void game_paddle_set_paddle_pos(game_paddle_app_t *self, uint16_t touch_y);
void game_paddle_tick(game_paddle_app_t *self);
uint16_t game_paddle_get_score(const game_paddle_app_t *self);
uint16_t game_paddle_get_high_score(const game_paddle_app_t *self);
void game_paddle_get_ball_pos(const game_paddle_app_t *self, int16_t *out_x, int16_t *out_y);
uint16_t game_paddle_get_paddle_pos(const game_paddle_app_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_GAME_PADDLE_H */
