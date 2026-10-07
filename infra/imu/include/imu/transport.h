#ifndef IMU_TRANSPORT_H
#define IMU_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The register access an IMU driver needs, without knowing which bus it is talking over. The
 * reference's own transport.h says the same thing: dev_addr is the seven-bit I2C address and is
 * ignored by the SPI transports, and the SPI read direction bit belongs to the transport, so a
 * driver always passes a clean register number.
 *
 * This port adds one thing the reference gets from the RTOS: a short delay, which its own drivers
 * take with chThdSleepMicroseconds. Here it is the bus's, so that a driver can be written and
 * tested without one.
 */
#define IMU_MAX_BURST 16u

typedef struct imu_transport imu_transport_t;

typedef struct imu_transport_interface {
    /* Human-readable transport name for diagnostics, e.g. "i2c-bb". */
    const char *name;
    /*
     * True when a transfer occupies the CPU for its whole duration, which is what decides whether
     * the read loop can run above normal priority. The reference keeps it for the bit-banged buses.
     */
    bool cpu_bound;
    /* The highest sample rate this bus may be driven at; each bus knows its own safe maximum. */
    uint16_t (*max_sample_rate)(imu_transport_t *self);
    bool (*read_reg)(imu_transport_t *self, uint8_t dev_addr, uint8_t reg, uint8_t *rx, size_t len);
    bool (*write_reg)(imu_transport_t *self, uint8_t dev_addr, uint8_t reg, const uint8_t *tx,
                      size_t len);
    /* Recover a stuck bus, e.g. a slave still holding the data line low. NULL when unsupported. */
    void (*recover)(imu_transport_t *self);
    void (*deinit)(imu_transport_t *self);
    /* A short pause, the reference's chThdSleepMicroseconds. */
    void (*delay_us)(imu_transport_t *self, uint32_t us);
} imu_transport_interface_t;

struct imu_transport {
    const imu_transport_interface_t *interface;
    void *self;
};

static inline bool imu_transport_read_reg(imu_transport_t *t, uint8_t dev_addr, uint8_t reg,
                                          uint8_t *rx, size_t len) {
    return t->interface->read_reg(t, dev_addr, reg, rx, len);
}

static inline bool imu_transport_write_reg(imu_transport_t *t, uint8_t dev_addr, uint8_t reg,
                                           const uint8_t *tx, size_t len) {
    return t->interface->write_reg(t, dev_addr, reg, tx, len);
}

static inline void imu_transport_recover(imu_transport_t *t) {
    if (t->interface->recover != (void *)0) {
        t->interface->recover(t);
    }
}

static inline void imu_transport_deinit(imu_transport_t *t) {
    if (t != (imu_transport_t *)0 && t->interface != (const imu_transport_interface_t *)0 &&
        t->interface->deinit != (void *)0) {
        t->interface->deinit(t);
    }
}

static inline uint16_t imu_transport_max_sample_rate(imu_transport_t *t) {
    return t->interface->max_sample_rate(t);
}

static inline void imu_transport_delay_us(imu_transport_t *t, uint32_t us) {
    if (t != (imu_transport_t *)0 && t->interface->delay_us != (void *)0) {
        t->interface->delay_us(t, us);
    }
}

#endif /* IMU_TRANSPORT_H */
