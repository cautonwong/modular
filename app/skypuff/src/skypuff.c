#include "skypuff/skypuff.h"

#include <math.h>

/* applications/app_skypuff.c:62-63's two derived quantities. */
float skypuff_meters_per_rev(float wheel_diameter, float gear_ratio) {
    return wheel_diameter / gear_ratio * (float)3.14159265358979323846;
}

float skypuff_steps_per_rev(float motor_poles) {
    return motor_poles * SKYPUFF_STEPS_PER_POLE;
}

/* :132-137, the reference's own line: the steps over a revolution of them, in the line's metres. */
float skypuff_steps_to_meters(int steps, float steps_per_rev, float meters_per_rev) {
    if (steps_per_rev == 0.0f) {
        return 0.0f;
    }
    return (float)steps / steps_per_rev * meters_per_rev;
}

/* :139-148: metres a second to revolutions a minute, then to the electrical speed the motor's own
 * pole pairs make of them. */
float skypuff_ms_to_erpm(float ms, float meters_per_rev, float motor_poles) {
    if (meters_per_rev == 0.0f) {
        return 0.0f;
    }
    const float rps = ms / meters_per_rev;
    const float rpm = rps * 60.0f;
    return rpm * (motor_poles / 2.0f);
}

/* :150-157: the same way round, in the reference's own order. */
float skypuff_erpm_to_ms(float erpm, float meters_per_rev, float motor_poles) {
    if (motor_poles == 0.0f) {
        return 0.0f;
    }
    const float erps = erpm / 60.0f;
    const float rps = erps / (motor_poles / 2.0f);
    return rps * meters_per_rev;
}

/*
 * :176-208: the three states that drive anything take the direction from the tachometer's own sign
 * - a negative count is commanded the magnitude as it stands, a positive one its negative - so that
 * the line winds the way it is already going.
 */
float skypuff_signed_command(float tacho, float magnitude) {
    return (tacho < 0.0f) ? magnitude : -magnitude;
}
