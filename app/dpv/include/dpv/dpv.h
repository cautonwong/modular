#ifndef APP_DPV_H
#define APP_DPV_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * applications/app_dpv.c:31-34's own four speeds and the rest of its arithmetic: a dive vehicle's
 * speed law, which is a potentiometer's angle turned into a target and then walked towards at a
 * rate the law itself picks. The thread, the timer that wakes it and the hall trigger it reads are
 * the product's; what is here is the law they drive.
 */
#define DPV_SPEED_MIN 0.10f
#define DPV_SPEED_MAX 1.00f
#define DPV_SPEED_OFF 0.00f

/* :105-110: the angle's own window, and the jump that means the pot is sprung rather than turned.
 */
#define DPV_ANGLE_MIN 400.0f
#define DPV_ANGLE_MAX 3800.0f
#define DPV_ANGLE_JUMP 1000

/*
 * :104-113: the angle is rejected when it moved further than that since the last one - which keeps
 * the value it had - then clamped into its window and mapped onto the two speeds.
 */
float dpv_speed_from_angle(float angle, float *last_angle);

/*
 * :131-137: the ramp's own time, which is five seconds on the way up and half of one on the way
 * down
 * - and then three seconds whenever the target is anything but nought, which is the reference's own
 * override rather than a tidy one: what it writes is the smaller of three and three.
 */
float dpv_ramp_time(float target_speed, float ramped_speed);

/*
 * :138-143: one step of the walk towards the target, at however much of the ramp the elapsed
 * milliseconds are worth - which is the reference's own step-towards, so a step past the target
 * lands on it rather than beyond.
 */
float dpv_ramp_step(float *ramped_speed, float target_speed, float elapsed_ms, float ramp_time);

#ifdef __cplusplus
}
#endif

#endif /* APP_DPV_H */
