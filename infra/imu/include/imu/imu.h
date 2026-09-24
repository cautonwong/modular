#ifndef INFRA_IMU_H
#define INFRA_IMU_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct imu_raw_data {
    float acc_x_g;
    float acc_y_g;
    float acc_z_g;
    float gyro_x_rad_s;
    float gyro_y_rad_s;
    float gyro_z_rad_s;
} imu_raw_data_t;

typedef struct imu_ahrs {
    float q0, q1, q2, q3;
    float roll_rad;
    float pitch_rad;
    float yaw_rad;
    float kp;
    float ki;
    float e_int_x;
    float e_int_y;
    float e_int_z;
} imu_ahrs_t;

void imu_ahrs_init(imu_ahrs_t *self, float kp, float ki);
edge_status_t imu_ahrs_update(imu_ahrs_t *self, float gx, float gy, float gz, float ax, float ay,
                              float az, float dt);
void imu_ahrs_get_euler(const imu_ahrs_t *self, float *roll, float *pitch, float *yaw);

#ifdef __cplusplus
}
#endif

#endif /* INFRA_IMU_H */
