#ifndef APP_SKYPUFF_H
#define APP_SKYPUFF_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * applications/app_skypuff.c's own arithmetic, which is a winch's: everything it says is in steps
 * of the tachometer, metres of line and metres a second, and the two quantities that connect them
 * are derived from the configuration - a revolution of the motor is the wheel's own circumference
 * over the gear ratio, and its steps are three per pole. The state machine that drives the motor
 * with it is the product's; what is here is the arithmetic it is driven by.
 */
#define SKYPUFF_STEPS_PER_POLE 3.0f

float skypuff_meters_per_rev(float wheel_diameter, float gear_ratio);
float skypuff_steps_per_rev(float motor_poles);

/* :132-137: the tachometer's steps as the line they have wound. */
float skypuff_steps_to_meters(int steps, float steps_per_rev, float meters_per_rev);

/* :139-148: metres a second as the motor's own electrical speed, and back (:150-157). */
float skypuff_ms_to_erpm(float ms, float meters_per_rev, float motor_poles);
float skypuff_erpm_to_ms(float erpm, float meters_per_rev, float motor_poles);

/*
 * :176-208: the direction the reference takes from the tachometer's own sign, in all three states
 * that drive anything - a negative count commands the magnitude as it stands and a positive one
 * commands its negative, so that the line winds the way the count is already going.
 */
float skypuff_signed_command(float tacho, float magnitude);

#ifdef __cplusplus
}
#endif

#endif /* APP_SKYPUFF_H */
