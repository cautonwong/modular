#include "dice/dice.h"

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    if (x == 0) {
        x = 0x12345678u;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static edge_status_t dice_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t dice_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t dice_power_off(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

void dice_construct(dice_app_t *self, uint32_t module_id, uint32_t priority,
                    const dice_entropy_if_t *entropy, const dice_motor_if_t *motor) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module.module_id = module_id;
    self->module.priority = priority;
    self->module.period = 100u;
    self->module.budget = 1u;
    self->module.next_due = 0u;
    self->module.poll = dice_poll;
    self->module.on_event = dice_on_event;
    self->module.power_off = dice_power_off;
    self->module.private_data = self;

    self->entropy = entropy;
    self->motor = motor;
    self->count = 1u;
    self->sides = DICE_D6;
    self->prng_state = 0xACE1ACE1u;

    if (entropy != NULL && entropy->get_entropy_seed != NULL) {
        uint32_t seed = entropy->get_entropy_seed(entropy->self);
        if (seed != 0) {
            self->prng_state = seed;
        }
    }

    /* Initial roll */
    (void)dice_roll(self);
}

edge_status_t dice_init(dice_app_t *self, const dice_entropy_if_t *entropy,
                        const dice_motor_if_t *motor) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    dice_construct(self, 0x3A00u, 50u, entropy, motor);
    return EDGE_OK;
}

edge_status_t dice_shutdown(dice_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->count = 1u;
    self->total_sum = 0u;
    self->roll_count = 0u;
    return EDGE_OK;
}

edge_status_t dice_set_count(dice_app_t *self, uint8_t count) {
    if (self == NULL || count < 1u || count > DICE_MAX_COUNT) {
        return EDGE_EINVAL;
    }
    self->count = count;
    return dice_roll(self);
}

uint8_t dice_get_count(const dice_app_t *self) {
    return self != NULL ? self->count : 1u;
}

edge_status_t dice_set_sides(dice_app_t *self, dice_sides_t sides) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    switch (sides) {
    case DICE_D4:
    case DICE_D6:
    case DICE_D8:
    case DICE_D10:
    case DICE_D12:
    case DICE_D20:
    case DICE_D100:
        self->sides = sides;
        return dice_roll(self);
    default:
        return EDGE_EINVAL;
    }
}

dice_sides_t dice_get_sides(const dice_app_t *self) {
    return self != NULL ? self->sides : DICE_D6;
}

edge_status_t dice_roll(dice_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }

    self->roll_count++;
    self->total_sum = 0;

    for (uint8_t i = 0; i < self->count; ++i) {
        uint32_t val = (xorshift32(&self->prng_state) % (uint32_t)self->sides) + 1u;
        self->results[i] = (uint8_t)val;
        self->total_sum += val;
    }

    if (self->motor != NULL && self->motor->vibrate != NULL) {
        (void)self->motor->vibrate(self->motor->self);
    }

    return EDGE_OK;
}

uint8_t dice_get_result(const dice_app_t *self, uint8_t index) {
    if (self == NULL || index >= self->count) {
        return 0;
    }
    return self->results[index];
}

uint32_t dice_get_total_sum(const dice_app_t *self) {
    return self != NULL ? self->total_sum : 0;
}
