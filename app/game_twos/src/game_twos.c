#include "game_twos/game_twos.h"

static uint32_t get_rnd(game_twos_app_t *self) {
    if (self != NULL && self->entropy_port != NULL && self->entropy_port->get_random != NULL) {
        return self->entropy_port->get_random(self->entropy_port->self);
    }
    return 42u;
}

static bool place_new_tile(game_twos_app_t *self) {
    uint8_t empty_cells[TWOS_GRID_CELLS];
    uint8_t n_empty = 0;

    for (uint8_t r = 0; r < TWOS_GRID_ROWS; r++) {
        for (uint8_t c = 0; c < TWOS_GRID_COLS; c++) {
            if (self->grid[r][c].value == 0) {
                empty_cells[n_empty++] = (uint8_t)(r * TWOS_GRID_COLS + c);
            }
        }
    }

    if (n_empty == 0) {
        return false;
    }

    uint32_t rnd = get_rnd(self);
    uint8_t pick = (uint8_t)(rnd % n_empty);
    uint8_t chosen_cell = empty_cells[pick];
    uint8_t row = (uint8_t)(chosen_cell / TWOS_GRID_COLS);
    uint8_t col = (uint8_t)(chosen_cell % TWOS_GRID_COLS);

    self->grid[row][col].value = (uint16_t)(((rnd / n_empty) % 100u < 90u) ? 2u : 4u);
    self->grid[row][col].merged = false;
    return true;
}

static bool try_merge(game_twos_app_t *self, int new_r, int new_c, int old_r, int old_c) {
    if (self->grid[new_r][new_c].value == self->grid[old_r][old_c].value) {
        if (new_c != old_c || new_r != old_r) {
            if (!self->grid[new_r][new_c].merged) {
                self->grid[new_r][new_c].value *= 2u;
                self->score += self->grid[new_r][new_c].value;
                if (self->score > self->high_score) {
                    self->high_score = self->score;
                }
                self->grid[old_r][old_c].value = 0;
                self->grid[new_r][new_c].merged = true;
                return true;
            }
        }
    }
    return false;
}

static bool try_move(game_twos_app_t *self, int new_r, int new_c, int old_r, int old_c) {
    if ((new_c >= 0 && new_c != old_c) || (new_r >= 0 && new_r != old_r)) {
        self->grid[new_r][new_c].value = self->grid[old_r][old_c].value;
        self->grid[old_r][old_c].value = 0;
        return true;
    }
    return false;
}

static void check_game_over(game_twos_app_t *self) {
    for (int r = 0; r < TWOS_GRID_ROWS; r++) {
        for (int c = 0; c < TWOS_GRID_COLS; c++) {
            if (self->grid[r][c].value == 0) {
                self->game_over = false;
                return;
            }
            if (r + 1 < TWOS_GRID_ROWS && self->grid[r][c].value == self->grid[r + 1][c].value) {
                self->game_over = false;
                return;
            }
            if (c + 1 < TWOS_GRID_COLS && self->grid[r][c].value == self->grid[r][c + 1].value) {
                self->game_over = false;
                return;
            }
        }
    }
    self->game_over = true;
}

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
    (void)module;
    return EDGE_OK;
}

void game_twos_construct(game_twos_app_t *self, uint32_t module_id, uint32_t priority,
                         const twos_entropy_if_t *entropy) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module.module_id = module_id;
    self->module.priority = priority;
    self->module.period = 50u;
    self->module.budget = 0u;
    self->module.next_due = 0u;
    self->module.poll = app_poll;
    self->module.on_event = app_on_event;
    self->module.power_off = app_power_off;
    self->module.private_data = self;

    self->entropy_port = entropy;
}

edge_status_t game_twos_init(game_twos_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    game_twos_reset(self);
    return EDGE_OK;
}

edge_status_t game_twos_shutdown(game_twos_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    game_twos_reset(self);
    return EDGE_OK;
}

void game_twos_reset(game_twos_app_t *self) {
    if (self == NULL) {
        return;
    }
    for (size_t r = 0; r < 4; ++r) {
        for (size_t c = 0; c < 4; ++c) {
            self->grid[r][c] = (twos_tile_t){0};
        }
    }
    self->score = 0;
    self->game_over = false;
    place_new_tile(self);
    place_new_tile(self);
}

bool game_twos_swipe(game_twos_app_t *self, twos_direction_t dir) {
    if (self == NULL || self->game_over) {
        return false;
    }

    bool valid_move = false;
    for (int r = 0; r < TWOS_GRID_ROWS; r++) {
        for (int c = 0; c < TWOS_GRID_COLS; c++) {
            self->grid[r][c].merged = false;
        }
    }

    switch (dir) {
    case TWOS_DIR_LEFT:
        for (int c = 1; c < TWOS_GRID_COLS; c++) {
            for (int r = 0; r < TWOS_GRID_ROWS; r++) {
                if (self->grid[r][c].value > 0) {
                    int new_c = -1;
                    for (int p_c = c - 1; p_c >= 0; p_c--) {
                        if (self->grid[r][p_c].value == 0) {
                            new_c = p_c;
                        } else {
                            if (try_merge(self, r, p_c, r, c)) {
                                valid_move = true;
                            }
                            break;
                        }
                    }
                    if (try_move(self, r, new_c, r, c)) {
                        valid_move = true;
                    }
                }
            }
        }
        break;
    case TWOS_DIR_RIGHT:
        for (int c = TWOS_GRID_COLS - 2; c >= 0; c--) {
            for (int r = 0; r < TWOS_GRID_ROWS; r++) {
                if (self->grid[r][c].value > 0) {
                    int new_c = -1;
                    for (int p_c = c + 1; p_c < TWOS_GRID_COLS; p_c++) {
                        if (self->grid[r][p_c].value == 0) {
                            new_c = p_c;
                        } else {
                            if (try_merge(self, r, p_c, r, c)) {
                                valid_move = true;
                            }
                            break;
                        }
                    }
                    if (try_move(self, r, new_c, r, c)) {
                        valid_move = true;
                    }
                }
            }
        }
        break;
    case TWOS_DIR_UP:
        for (int r = 1; r < TWOS_GRID_ROWS; r++) {
            for (int c = 0; c < TWOS_GRID_COLS; c++) {
                if (self->grid[r][c].value > 0) {
                    int new_r = -1;
                    for (int p_r = r - 1; p_r >= 0; p_r--) {
                        if (self->grid[p_r][c].value == 0) {
                            new_r = p_r;
                        } else {
                            if (try_merge(self, p_r, c, r, c)) {
                                valid_move = true;
                            }
                            break;
                        }
                    }
                    if (try_move(self, new_r, c, r, c)) {
                        valid_move = true;
                    }
                }
            }
        }
        break;
    case TWOS_DIR_DOWN:
        for (int r = TWOS_GRID_ROWS - 2; r >= 0; r--) {
            for (int c = 0; c < TWOS_GRID_COLS; c++) {
                if (self->grid[r][c].value > 0) {
                    int new_r = -1;
                    for (int p_r = r + 1; p_r < TWOS_GRID_ROWS; p_r++) {
                        if (self->grid[p_r][c].value == 0) {
                            new_r = p_r;
                        } else {
                            if (try_merge(self, p_r, c, r, c)) {
                                valid_move = true;
                            }
                            break;
                        }
                    }
                    if (try_move(self, new_r, c, r, c)) {
                        valid_move = true;
                    }
                }
            }
        }
        break;
    }

    if (valid_move) {
        place_new_tile(self);
        check_game_over(self);
    }
    return valid_move;
}

uint32_t game_twos_get_score(const game_twos_app_t *self) {
    return self != NULL ? self->score : 0;
}

uint32_t game_twos_get_high_score(const game_twos_app_t *self) {
    return self != NULL ? self->high_score : 0;
}

uint16_t game_twos_get_cell(const game_twos_app_t *self, uint8_t row, uint8_t col) {
    if (self != NULL && row < TWOS_GRID_ROWS && col < TWOS_GRID_COLS) {
        return self->grid[row][col].value;
    }
    return 0;
}

bool game_twos_is_game_over(const game_twos_app_t *self) {
    return self != NULL && self->game_over;
}
