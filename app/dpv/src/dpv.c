#include "dpv/dpv.h"

#include <math.h>

/*
 * applications/app_dpv.c:104-113. The reading is the potentiometer's own, and one that moved
 * further than a thousand counts since the last one is a pot that sprang back rather than a hand
 * that turned it: the reference keeps the value it had, and leaves the remembered reading alone.
 * What survives is clamped into the window the two speeds span and mapped onto them.
 */
float dpv_speed_from_angle(float angle, float *last_angle) {
    if (last_angle != (void *)0) {
        const int diff = (int)(angle - *last_angle);
        if ((diff > DPV_ANGLE_JUMP) || (diff < -DPV_ANGLE_JUMP)) {
            angle = *last_angle;
        } else {
            *last_angle = angle;
        }
    }

    if (angle > DPV_ANGLE_MAX) {
        angle = DPV_ANGLE_MAX;
    }
    if (angle < DPV_ANGLE_MIN) {
        angle = DPV_ANGLE_MIN;
    }

    /* utils_map's own line (utils_math.h:206-208), over the window and the two speeds. */
    return (angle - DPV_ANGLE_MIN) * (DPV_SPEED_MAX - DPV_SPEED_MIN) /
               (DPV_ANGLE_MAX - DPV_ANGLE_MIN) +
           DPV_SPEED_MIN;
}

/*
 * :131-137: five seconds towards more speed and half of one towards less, and then three seconds
 * whenever the target is more than a hundredth - which is the reference's own override, and the
 * reason its two arguments are both three. It is reproduced rather than tidied: what it writes is
 * the smaller of three and three.
 */
float dpv_ramp_time(float target_speed, float ramped_speed) {
    float ramp_time = (fabsf(target_speed) > fabsf(ramped_speed)) ? 5.0f : 0.5f;

    if (fabsf(target_speed) > 0.01f) {
        const float three = 3.0f;
        ramp_time = fminf(three, three);
    }

    return ramp_time;
}

/*
 * :138-143: the step is the elapsed milliseconds over the ramp's own thousandths, and the walk
 * towards the target is the reference's own - a step that is further than what is left lands on the
 * target rather than beyond it.
 */
float dpv_ramp_step(float *ramped_speed, float target_speed, float elapsed_ms, float ramp_time) {
    if (ramped_speed == (void *)0) {
        return 0.0f;
    }
    if (ramp_time <= 0.01f) {
        return *ramped_speed;
    }

    const float step = elapsed_ms / (ramp_time * 1000.0f);
    const float diff = target_speed - *ramped_speed;

    if (diff > step) {
        *ramped_speed += step;
    } else if (diff < -step) {
        *ramped_speed -= step;
    } else {
        *ramped_speed = target_speed;
    }

    return *ramped_speed;
}
