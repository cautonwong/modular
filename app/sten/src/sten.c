#include "sten/sten.h"

#include <math.h>

/* applications/app_sten.c:76-88: the middle of three, by the reference's own comparisons. */
uint16_t sten_middle_of_three(uint16_t a, uint16_t b, uint16_t c) {
    uint16_t middle;

    if ((a <= b) && (a <= c)) {
        middle = (b <= c) ? b : c;
    } else if ((b <= a) && (b <= c)) {
        middle = (a <= c) ? a : c;
    } else {
        middle = (a <= b) ? a : b;
    }

    return middle;
}

/*
 * :83-90: the middle of the three bytes over the centre, less one - a reading between minus one and
 * one - with the two before it remembered as they are. A caller with nowhere to remember them gets
 * the byte taken against a resting one.
 */
float sten_output_from_packet(uint16_t c, uint16_t *c1, uint16_t *c2) {
    const uint16_t previous1 = (c1 != (void *)0) ? *c1 : (uint16_t)STEN_PACKET_CENTRE;
    const uint16_t previous2 = (c2 != (void *)0) ? *c2 : (uint16_t)STEN_PACKET_CENTRE;

    const uint16_t med = sten_middle_of_three(c, previous1, previous2);

    if (c1 != (void *)0) {
        *c2 = *c1;
        *c1 = c;
    }

    return ((float)med / STEN_PACKET_CENTRE) - 1.0f;
}

/*
 * utils_math.c:57-66, the reference's own deadband: below the threshold the value is nought, and
 * past it the value is rescaled so that it resumes there rather than jumping.
 */
void sten_deadband(float *value, float threshold, float max) {
    if (value == (void *)0) {
        return;
    }

    if (fabsf(*value) < threshold) {
        *value = 0.0f;
    } else {
        const float k = max / (max - threshold);
        if (*value > 0.0f) {
            *value = k * *value + max * (1.0f - k);
        } else {
            *value = -(k * -*value + max * (1.0f - k));
        }
    }
}

/*
 * :46-60's soft ceiling: past the second speed the current is the floor's own negative, past the
 * first it is mapped down to it over the two - the reference's own map, which is a straight line
 * from what the current was to that floor - and below both it is what it was.
 */
float sten_soft_rpm_limit(float current_a, float rpm, float rpm_max_1, float rpm_max_2,
                          float cc_min_current) {
    if (rpm > rpm_max_2) {
        return -cc_min_current;
    }
    if (rpm > rpm_max_1) {
        return (rpm - rpm_max_1) * (-cc_min_current - current_a) / (rpm_max_2 - rpm_max_1) +
               current_a;
    }
    return current_a;
}

/* :64-68's three-tap average, whose two remembered values are the caller's. */
float sten_smooth3(float *p1, float *p2, float current_a) {
    if (p1 == (void *)0 || p2 == (void *)0) {
        return current_a;
    }

    const float smoothed = (current_a + *p1 + *p2) / 3.0f;
    *p2 = *p1;
    *p1 = smoothed;

    return smoothed;
}

/*
 * :42-71's deciding half, over a reading the deadband has already had. A positive one with the
 * speed still ahead of the brake's own limit is a current - the reading over the machine's maximum,
 * wound down by the soft ceiling - and anything else is a brake at the reading over its minimum.
 * The reference's own asymmetry is here as well: its current branch tests the sign of a reading it
 * has just established to be positive, which is why the other side of that test does nothing.
 */
sten_output_t sten_output_command(float output, float rpm, float l_current_max, float l_current_min,
                                  float l_max_erpm_fbrake, float cc_min_current) {
    sten_output_t command = {.brake = false, .current_a = 0.0f};

    if (output > 0.0f && rpm > -l_max_erpm_fbrake) {
        const float current = output * l_current_max;
        command.current_a =
            sten_soft_rpm_limit(current, rpm, STEN_RPM_MAX_1, STEN_RPM_MAX_2, cc_min_current);
    } else {
        command.brake = true;
        command.current_a = output * l_current_min;
    }

    return command;
}
