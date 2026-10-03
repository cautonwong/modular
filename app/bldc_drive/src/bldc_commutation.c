#include "bldc_drive/bldc_commutation.h"

#include <math.h>
#include <string.h>

void bldc_build_hall_tables(const int8_t hall_to_phase[8], int8_t forward[8], int8_t reverse[8]) {
    /* mcpwm.c:502: the reference's own map, indexed by the step it stored. */
    static const int8_t fwd_to_rev[7] = {-1, 1, 6, 5, 4, 3, 2};

    if (hall_to_phase == (void *)0 || forward == (void *)0 || reverse == (void *)0) {
        return;
    }

    for (int i = 0; i < 8; i++) {
        const int8_t step = hall_to_phase[i];
        reverse[i] = step;

        /*
         * The reference's own two cases are "below one" and everything else, and its map has seven
         * entries because its table only ever holds one to six. A table that holds something else
         * is not one this layer produced, so it is passed through rather than indexed with - a read
         * past the map is the one thing this port does not reproduce.
         */
        if (step < 1 || step > 6) {
            forward[i] = step;
            continue;
        }

        forward[i] = fwd_to_rev[step];
    }
}

int bldc_comm_step_advance(int comm_step, int steps) {
    /* mcpwm.c:2597-2603, its two whiles rather than a modulo, so the arithmetic is the same one. */
    comm_step += steps;
    while (comm_step > 6) {
        comm_step -= 6;
    }
    while (comm_step < 1) {
        comm_step += 6;
    }
    return comm_step;
}

int bldc_tacho_step_delta(int comm_step, int last_step) {
    /* mcpwm.c:2559-2566. */
    const int step = comm_step - 1;
    int tacho_diff = (step - last_step) % 6;

    if (tacho_diff > 3) {
        tacho_diff -= 6;
    } else if (tacho_diff < -2) {
        tacho_diff += 6;
    }

    return tacho_diff;
}

bool bldc_sensorless_now(uint8_t sensor_mode, float rpm, float hall_sl_erpm) {
    /* mcpwm.c:2587-2593, whose speed comparison is against the absolute value of its own rpm. */
    return (sensor_mode == BLDC_SENSOR_MODE_SENSORLESS) ||
           ((sensor_mode == BLDC_SENSOR_MODE_HYBRID) && (fabsf(rpm) > hall_sl_erpm));
}

uint8_t bldc_hall_phase(bool hall1, bool hall2, bool hall3) {
    /* mcpwm.c:2307, mcpwm_read_hall_phase, its own bit order. */
    return (uint8_t)((hall1 ? 1u : 0u) | (hall2 ? 2u : 0u) | (hall3 ? 4u : 0u));
}

void bldc_hall_commutation(int comm_step, int hall_phase, bool running, bool has_commutated,
                           bldc_hall_commutation_t *out) {
    if (out == (void *)0) {
        return;
    }

    /* mcpwm.c:1939-1952, in the reference's own order. */
    out->comm_step = comm_step;
    out->step_changed = false;
    out->apply = false;

    if (comm_step != hall_phase) {
        out->comm_step = hall_phase;
        out->step_changed = true;
        if (running) {
            out->apply = true;
        }
    } else if (running && !has_commutated) {
        out->apply = true;
    }
}

int bldc_hall_phase_from_table(const int8_t forward[8], const int8_t reverse[8], uint8_t reading,
                               int direction) {
    if (forward == (void *)0 || reverse == (void *)0) {
        return 0;
    }

    /*
     * mcpwm.c:2303: one array, indexed with the direction's own offset. This port keeps the two
     * halves as two arrays, so the same lookup is the choice between them - and the reading is
     * taken to its three bits, which the reference's own read_hall() already guarantees.
     */
    const int index = (int)(reading & 7u);
    return (int)((direction != 0) ? reverse[index] : forward[index]);
}

void bldc_hall_detect_reset(bldc_hall_detect_counts_t counts) {
    /* mcpwm.c:2243's memset over the same eight by seven. */
    memset(counts, 0, sizeof(int) * 8u * 7u);
}

void bldc_hall_detect_sample(bldc_hall_detect_counts_t counts, uint8_t reading, int comm_step,
                             bool in_first_half) {
    /* mcpwm.c:1874-1877: the gate is the caller's to make, and the column is the step it is at. */
    if (!in_first_half || comm_step < 1 || comm_step > 6) {
        return;
    }

    counts[reading & 7u][comm_step]++;
}

int bldc_hall_detect_result(bldc_hall_detect_counts_t counts, bool hall_sensor_port,
                            int8_t out[8]) {
    if (!hall_sensor_port || out == (void *)0) {
        return hall_sensor_port ? -1 : -3;
    }

    /* mcpwm.c:2262-2275: each reading's step is the one with the most samples, and only a reading
     * with more than fifteen of them names anything. */
    for (int i = 0; i < 8; i++) {
        int samples = 0;
        int res = -1;

        for (int j = 1; j < 7; j++) {
            if (counts[i][j] > samples) {
                samples = counts[i][j];
                if (samples > BLDC_HALL_DETECT_MIN_SAMPLES) {
                    res = j;
                }
            }
            out[i] = (int8_t)res;
        }
    }

    /* mcpwm.c:2279-2297: a table reads back when exactly two readings name nothing and the other
     * six name six different steps. */
    int invalid_samp_num = 0;
    int nums[7] = {0, 0, 0, 0, 0, 0, 0};
    int tot_nums = 0;

    for (int i = 0; i < 8; i++) {
        if (out[i] == -1) {
            invalid_samp_num++;
        } else if (nums[out[i]] == 0) {
            nums[out[i]] = 1;
            tot_nums++;
        }
    }

    return (invalid_samp_num == 2 && tot_nums == 6) ? 0 : -1;
}
