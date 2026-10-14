#include "imu/bmi160_wrapper.h"

#include <stddef.h>

/* The two pauses the reference takes with the RTOS, in microseconds. */
#define BMI160_SETTLE_DELAY_US 50000u
#define BMI160_FAIL_DELAY_US 5000u

/*
 * Bosch's callbacks carry no context pointer, which is the reference's own reason for holding the
 * transport in a file-scope variable: one BMI160 at a time. That is kept as it is rather than
 * pretended away.
 */
static imu_transport_t *m_transport;

static int8_t bmi_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len) {
    return imu_transport_read_reg(m_transport, dev_addr, reg_addr, data, len) ? BMI160_OK
                                                                              : BMI160_E_COM_FAIL;
}

static int8_t bmi_write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len) {
    return imu_transport_write_reg(m_transport, dev_addr, reg_addr, data, len) ? BMI160_OK
                                                                               : BMI160_E_COM_FAIL;
}

static void user_delay_ms(uint32_t ms) {
    imu_transport_delay_us(m_transport, ms * 1000u);
}

/*
 * applications/imu/bmi160_wrapper.c:47-92, the whole of reset_init_bmi. Both parts run at their
 * highest rate so that an asynchronous poll reads the freshest sample, and the filter setting picks
 * an oversampling ratio instead of a cutoff: with the rate pinned, that ratio is what decides the
 * cutoff. The reference notes the consequence - a sample rate below about three hundred and fifty
 * hertz cannot be fully anti-aliased this way - and leaves it.
 */
static bool reset_init_bmi(struct bmi160_dev *sensor, imu_filter_t filter) {
    sensor->delay_ms = user_delay_ms;
    (void)bmi160_init(sensor);

    sensor->accel_cfg.range = BMI160_ACCEL_RANGE_16G;
    sensor->accel_cfg.power = BMI160_ACCEL_NORMAL_MODE;
    sensor->accel_cfg.odr = BMI160_ACCEL_ODR_1600HZ;

    sensor->gyro_cfg.range = BMI160_GYRO_RANGE_2000_DPS;
    sensor->gyro_cfg.power = BMI160_GYRO_NORMAL_MODE;
    sensor->gyro_cfg.odr = BMI160_GYRO_ODR_3200HZ;

    if (filter == IMU_FILTER_HIGH) {
        sensor->accel_cfg.bw = BMI160_ACCEL_BW_OSR4_AVG1;
        sensor->gyro_cfg.bw = BMI160_GYRO_BW_OSR4_MODE;
    } else if (filter == IMU_FILTER_MEDIUM) {
        sensor->accel_cfg.bw = BMI160_ACCEL_BW_OSR2_AVG2;
        sensor->gyro_cfg.bw = BMI160_GYRO_BW_OSR2_MODE;
    } else {
        sensor->accel_cfg.bw = BMI160_ACCEL_BW_NORMAL_AVG4;
        sensor->gyro_cfg.bw = BMI160_GYRO_BW_NORMAL_MODE;
    }

    imu_transport_delay_us(m_transport, BMI160_SETTLE_DELAY_US);
    const int8_t res = bmi160_set_sens_conf(sensor);
    imu_transport_delay_us(m_transport, BMI160_SETTLE_DELAY_US);
    return res == BMI160_OK;
}

/* applications/imu/bmi160_wrapper.c:93-108. */
static bool configure(imu_device_t *dev, imu_filter_t filter, bool use_mag) {
    (void)use_mag;

    struct bmi160_dev *sensor = dev->priv;
    m_transport = dev->transport;

    sensor->interface = (dev->dev_addr != 0u) ? BMI160_I2C_INTF : BMI160_SPI_INTF;
    sensor->id = dev->dev_addr;
    sensor->read = bmi_read;
    sensor->write = bmi_write;
    return reset_init_bmi(sensor, filter);
}

/* applications/imu/bmi160_wrapper.c:109-127: the vendor driver's own read, in its own units. */
static bool read_sample(imu_device_t *dev, float accel[3], float gyro[3], float mag[3]) {
    struct bmi160_dev *sensor = dev->priv;
    struct bmi160_sensor_data a;
    struct bmi160_sensor_data g;

    if (bmi160_get_sensor_data(BMI160_ACCEL_SEL | BMI160_GYRO_SEL, &a, &g, sensor) != BMI160_OK) {
        return false;
    }

    accel[0] = (float)a.x * 16.0f / 32768.0f;
    accel[1] = (float)a.y * 16.0f / 32768.0f;
    accel[2] = (float)a.z * 16.0f / 32768.0f;
    gyro[0] = (float)g.x * 2000.0f / 32768.0f;
    gyro[1] = (float)g.y * 2000.0f / 32768.0f;
    gyro[2] = (float)g.z * 2000.0f / 32768.0f;
    mag[0] = 0.0f;
    mag[1] = 0.0f;
    mag[2] = 0.0f;
    return true;
}

/* applications/imu/bmi160_wrapper.c:129-134: the vendor driver recovers by itself, so this only
 * backs off before the next read. */
static void on_read_fail(imu_device_t *dev) {
    (void)dev;
    imu_transport_delay_us(m_transport, BMI160_FAIL_DELAY_US);
}

static const imu_device_interface_t bmi160_interface = {
    .name = "BMI160",
    .configure = configure,
    .read_sample = read_sample,
    .on_read_fail = on_read_fail,
    /* The reference gives this one no data-ready wiring. */
    .enable_drdy_output = NULL,
};

imu_device_t bmi160_device(imu_transport_t *transport, uint8_t interface,
                           struct bmi160_dev *sensor) {
    return (imu_device_t){
        .interface = &bmi160_interface,
        .transport = transport,
        .priv = sensor,
        .dev_addr = (interface == BMI160_I2C_INTF) ? BMI160_I2C_ADDR : 0u,
    };
}
