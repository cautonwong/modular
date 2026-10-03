#include "game_paddle/game_paddle.h"

static edge_status_t app_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t app_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t app_power_off(edge_module_t *module) {
    game_paddle_app_t *self = (game_paddle_app_t *)module->private_data;
    if (self != NULL) {
        self->is_running = false;
    }
    return EDGE_OK;
}

void game_paddle_construct(game_paddle_app_t *self, uint32_t module_id, uint32_t priority,
                           const paddle_motor_if_t *motor, const paddle_entropy_if_t *entropy) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module.module_id = module_id;
    self->module.priority = priority;
    self->module.period = 20u;
    self->module.budget = 0u;
    self->module.next_due = 0u;
    self->module.poll = app_poll;
    self->module.on_event = app_on_event;
    self->module.power_off = app_power_off;
    self->module.private_data = self;

    self->motor_port = motor;
    self->entropy_port = entropy;
}

edge_status_t game_paddle_init(game_paddle_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    game_paddle_reset(self);
    return EDGE_OK;
}

edge_status_t game_paddle_shutdown(game_paddle_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->is_running = false;
    return EDGE_OK;
}

void game_paddle_reset(game_paddle_app_t *self) {
    if (self == NULL) {
        return;
    }
    self->paddle_y = PADDLE_SCREEN_HEIGHT / 2;
    self->ball_x = (PADDLE_SCREEN_WIDTH - PADDLE_BALL_SIZE) / 2;
    self->ball_y = (PADDLE_SCREEN_HEIGHT - PADDLE_BALL_SIZE) / 2;
    self->dx = 2;
    self->dy = 3;
    self->score = 0;
    self->is_running = true;
}

void game_paddle_set_paddle_pos(game_paddle_app_t *self, uint16_t touch_y) {
    if (self == NULL) {
        return;
    }
    if (touch_y < 31u) {
        self->paddle_y = 30u;
    } else if (touch_y > PADDLE_SCREEN_HEIGHT - 31u) {
        self->paddle_y = PADDLE_SCREEN_HEIGHT - 31u;
    } else {
        self->paddle_y = touch_y;
    }
}

void game_paddle_tick(game_paddle_app_t *self) {
    if (self == NULL || !self->is_running) {
        return;
    }

    self->ball_x += self->dx;
    self->ball_y += self->dy;

    /* Floor and ceiling collision */
    if (self->ball_y <= 1 || self->ball_y >= PADDLE_SCREEN_HEIGHT - PADDLE_BALL_SIZE - 2) {
        self->dy *= -1;
    }

    /* Right wall collision */
    if (self->ball_x >= PADDLE_SCREEN_WIDTH - PADDLE_BALL_SIZE - 1) {
        self->dx *= -1;
        int32_t rnd = 0;
        if (self->entropy_port != NULL && self->entropy_port->get_random != NULL) {
            rnd = (int32_t)(self->entropy_port->get_random(self->entropy_port->self) % 3u) - 1;
        }
        self->dy += (int8_t)rnd;
        if (self->dy > 5) {
            self->dy = 5;
        }
        if (self->dy < -5) {
            self->dy = -5;
        }
    }

    /* Paddle position collision */
    if (self->dx < 0 && self->ball_x <= 4) {
        const int16_t top_bound =
            (int16_t)(self->paddle_y - 30 - PADDLE_BALL_SIZE + PADDLE_BALL_SIZE / 4);
        const int16_t bot_bound = (int16_t)(self->paddle_y + 30 - PADDLE_BALL_SIZE / 4);
        if (self->ball_x >= -(PADDLE_BALL_SIZE / 4)) {
            if (self->ball_y >= top_bound && self->ball_y <= bot_bound) {
                self->dx *= -1;
                self->score++;
                if (self->score > self->high_score) {
                    self->high_score = self->score;
                }
            }
        } else if (self->ball_x <= -(PADDLE_BALL_SIZE * 2)) {
            /* Missed! Reset ball */
            if (self->motor_port != NULL && self->motor_port->vibrate != NULL) {
                self->motor_port->vibrate(self->motor_port->self, 50u);
            }
            self->ball_x = (PADDLE_SCREEN_WIDTH - PADDLE_BALL_SIZE) / 2;
            self->ball_y = (PADDLE_SCREEN_HEIGHT - PADDLE_BALL_SIZE) / 2;
            self->score = 0;
        }
    }
}

uint16_t game_paddle_get_score(const game_paddle_app_t *self) {
    return self != NULL ? self->score : 0;
}

uint16_t game_paddle_get_high_score(const game_paddle_app_t *self) {
    return self != NULL ? self->high_score : 0;
}

void game_paddle_get_ball_pos(const game_paddle_app_t *self, int16_t *out_x, int16_t *out_y) {
    if (self != NULL) {
        if (out_x != NULL) {
            *out_x = self->ball_x;
        }
        if (out_y != NULL) {
            *out_y = self->ball_y;
        }
    }
}

uint16_t game_paddle_get_paddle_pos(const game_paddle_app_t *self) {
    return self != NULL ? self->paddle_y : 0;
}
