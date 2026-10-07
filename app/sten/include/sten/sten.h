#ifndef APP_STEN_H
#define APP_STEN_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * applications/app_sten.c's own law, which is a remote control's byte turned into a current: the
 * bytes arrive one at a time, three of them are taken and their middle one is what says where the
 * stick is, a deadband around the middle keeps a resting stick quiet, and the current that comes
 * out is wound down as the motor approaches its own ceiling. The UART, the thread and the motor are
 * the product's; what is here is the law between them.
 */
#define STEN_HYST 0.10f
#define STEN_RPM_MAX_1 41000.0f /* where the output starts to be wound down */
#define STEN_RPM_MAX_2 44000.0f /* where it is wound down entirely */
#define STEN_PACKET_CENTRE 128.0f

/* :76-88: the middle of the three bytes last seen, which is what the stick's reading is taken from.
 */
uint16_t sten_middle_of_three(uint16_t a, uint16_t b, uint16_t c);

/*
 * :83-90: one byte in, the two before it remembered, and the reading out - the middle of the three
 * over its own centre, less one, which is a value between minus one and one. The first two bytes a
 * run ever sees are compared against a resting one rather than against nothing.
 */
float sten_output_from_packet(uint16_t c, uint16_t *c1, uint16_t *c2);

/*
 * utils_math.c:57-66, the reference's own deadband: below the threshold the value is nought, and
 * past it the value is rescaled so that it resumes there rather than jumping - the two constants
 * being the reference's own maximum and its own threshold.
 */
void sten_deadband(float *value, float threshold, float max);

/* :46-60's soft ceiling: past the second speed the current is the floor's own negative, past the
 * first it is mapped down to it over the two, and below both it is what it was. */
float sten_soft_rpm_limit(float current_a, float rpm, float rpm_max_1, float rpm_max_2,
                          float cc_min_current);

/* :64-68's own three-tap average, whose two remembered values are the caller's. */
float sten_smooth3(float *p1, float *p2, float current_a);

/*
 * :42-71's deciding half: a reading and a speed are either a current or a braking one. The current
 * is the reading over the machine's own maximum, and the brake is the reading over its minimum -
 * and the reference's own asymmetry is kept, in that the current branch tests the sign of a reading
 * it has already established to be positive.
 */
typedef struct sten_output {
    bool brake;
    float current_a;
} sten_output_t;

sten_output_t sten_output_command(float output, float rpm, float l_current_max, float l_current_min,
                                  float l_max_erpm_fbrake, float cc_min_current);

#ifdef __cplusplus
}
#endif

#endif /* APP_STEN_H */
