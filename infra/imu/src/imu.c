#include "imu/imu.h"
#include <math.h>
#include <string.h>

void imu_ahrs_init(imu_ahrs_t *self, float kp, float ki) {
    if (self == (void *)0) {
        return;
    }
    memset(self, 0, sizeof(*self));
    self->q0 = 1.0f;
    self->q1 = 0.0f;
    self->q2 = 0.0f;
    self->q3 = 0.0f;
    self->kp = kp > 0.0f ? kp : 2.0f;
    self->ki = ki >= 0.0f ? ki : 0.005f;
    self->e_int_x = 0.0f;
    self->e_int_y = 0.0f;
    self->e_int_z = 0.0f;
}

edge_status_t imu_ahrs_update(imu_ahrs_t *self, float gx, float gy, float gz, float ax, float ay,
                              float az, float dt) {
    if (self == (void *)0 || dt <= 0.0f) {
        return EDGE_EINVAL;
    }

    float q0 = self->q0;
    float q1 = self->q1;
    float q2 = self->q2;
    float q3 = self->q3;

    /* Compute feedback only if accelerometer measurement valid */
    float a_norm_sq = ax * ax + ay * ay + az * az;
    if (a_norm_sq > 1e-6f) {
        float a_recip = 1.0f / sqrtf(a_norm_sq);
        ax *= a_recip;
        ay *= a_recip;
        az *= a_recip;

        /* Estimated direction of gravity */
        float vx = 2.0f * (q1 * q3 - q0 * q2);
        float vy = 2.0f * (q0 * q1 + q2 * q3);
        float vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;

        /* Error is cross product between estimated and measured direction of gravity */
        float ex = (ay * vz - az * vy);
        float ey = (az * vx - ax * vz);
        float ez = (ax * vy - ay * vx);

        /* Integral feedback */
        if (self->ki > 0.0f) {
            self->e_int_x += ex * self->ki * dt;
            self->e_int_y += ey * self->ki * dt;
            self->e_int_z += ez * self->ki * dt;
            gx += self->e_int_x;
            gy += self->e_int_y;
            gz += self->e_int_z;
        }

        /* Proportional feedback */
        gx += self->kp * ex;
        gy += self->kp * ey;
        gz += self->kp * ez;
    }

    /* Integrate rate of change of quaternion */
    gx *= 0.5f * dt;
    gy *= 0.5f * dt;
    gz *= 0.5f * dt;

    float qa = q0;
    float qb = q1;
    float qc = q2;

    q0 += (-qb * gx - qc * gy - q3 * gz);
    q1 += (qa * gx + qc * gz - q3 * gy);
    q2 += (qa * gy - qb * gz + q3 * gx);
    q3 += (qa * gz + qb * gy - qc * gx);

    /* Normalise quaternion */
    float q_norm = 1.0f / sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    self->q0 = q0 * q_norm;
    self->q1 = q1 * q_norm;
    self->q2 = q2 * q_norm;
    self->q3 = q3 * q_norm;

    /* Euler angles */
    self->roll_rad = atan2f(2.0f * (self->q0 * self->q1 + self->q2 * self->q3),
                            1.0f - 2.0f * (self->q1 * self->q1 + self->q2 * self->q2));

    float sin_pitch = 2.0f * (self->q0 * self->q2 - self->q3 * self->q1);
    if (sin_pitch > 1.0f)
        sin_pitch = 1.0f;
    if (sin_pitch < -1.0f)
        sin_pitch = -1.0f;
    self->pitch_rad = asinf(sin_pitch);

    self->yaw_rad = atan2f(2.0f * (self->q0 * self->q3 + self->q1 * self->q2),
                           1.0f - 2.0f * (self->q2 * self->q2 + self->q3 * self->q3));

    return EDGE_OK;
}

void imu_ahrs_get_euler(const imu_ahrs_t *self, float *roll, float *pitch, float *yaw) {
    if (self == (void *)0) {
        return;
    }
    if (roll)
        *roll = self->roll_rad;
    if (pitch)
        *pitch = self->pitch_rad;
    if (yaw)
        *yaw = self->yaw_rad;
}
