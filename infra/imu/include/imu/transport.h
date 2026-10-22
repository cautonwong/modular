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

typedef struct imu_transport_interface {
    /* Human-readable transport name for diagnostics, e.g. "i2c-bb". */
    const char *name;
    /*
     * True when a transfer occupies the CPU for its whole duration, which is what decides whether
     * the read loop can run above normal priority. The reference keeps it for the bit-banged buses.
     */
    bool cpu_bound;
    /* The highest sample rate this bus may be driven at; each bus knows its own safe maximum. */
    uint16_t (*max_sample_rate)(void *self);
    bool (*read_reg)(void *self, uint8_t dev_addr, uint8_t reg, uint8_t *rx, size_t len);
    bool (*write_reg)(void *self, uint8_t dev_addr, uint8_t reg, const uint8_t *tx, size_t len);
    /* Recover a stuck bus, e.g. a slave still holding the data line low. NULL when unsupported. */
    void (*recover)(void *self);
    void (*deinit)(void *self);
    /* A short pause, the reference's chThdSleepMicroseconds. */
    void (*delay_us)(void *self, uint32_t us);
    void *self;
} imu_transport_interface_t;

/*
 * The handle is an opaque, caller-owned storage block (#258), same shape as
 * `imu_device_t`; the two fields live in src/imu_internal.h.
 */
#if UINTPTR_MAX > 0xFFFFFFFFu
#define IMU_TRANSPORT_STORAGE_SIZE 16u
#else
#define IMU_TRANSPORT_STORAGE_SIZE 8u
#endif
#define IMU_TRANSPORT_STORAGE_ALIGN 8u

typedef struct imu_transport {
    _Alignas(IMU_TRANSPORT_STORAGE_ALIGN) unsigned char storage[IMU_TRANSPORT_STORAGE_SIZE];
} imu_transport_t;

/* Construction-time prefix; see the note on `imu_device_prefix_t`. */
typedef struct imu_transport_prefix {
    const imu_transport_interface_t *interface;
    void *self;
} imu_transport_prefix_t;

void imu_transport_construct(imu_transport_t *t, const imu_transport_interface_t *interface,
                             void *self);

static inline const imu_transport_prefix_t *imu_transport_prefix(const imu_transport_t *t) {
    return (const imu_transport_prefix_t *)(const void *)(uintptr_t)t;
}

static inline const imu_transport_interface_t *imu_transport_interface(const imu_transport_t *t) {
    return t != (const imu_transport_t *)0 ? imu_transport_prefix(t)->interface : (const imu_transport_interface_t *)0;
}

static inline void *imu_transport_self(const imu_transport_t *t) {
    return t != (const imu_transport_t *)0 ? imu_transport_prefix(t)->self : (void *)0;
}

/* Hot path: register access happens once per sample, so it costs no extra call. */
static inline bool imu_transport_read_reg(imu_transport_t *t, uint8_t dev_addr, uint8_t reg,
                                          uint8_t *rx, size_t len) {
    return imu_transport_prefix(t)->interface->read_reg(t, dev_addr, reg, rx, len);
}

static inline bool imu_transport_write_reg(imu_transport_t *t, uint8_t dev_addr, uint8_t reg,
                                           const uint8_t *tx, size_t len) {
    return imu_transport_prefix(t)->interface->write_reg(t, dev_addr, reg, tx, len);
}

static inline void imu_transport_recover(imu_transport_t *t) {
    if (imu_transport_prefix(t)->interface->recover != (void *)0) {
        imu_transport_prefix(t)->interface->recover(t);
    }
}

static inline void imu_transport_deinit(imu_transport_t *t) {
    if (t != (imu_transport_t *)0 &&
        imu_transport_prefix(t)->interface != (const imu_transport_interface_t *)0 &&
        imu_transport_prefix(t)->interface->deinit != (void *)0) {
        imu_transport_prefix(t)->interface->deinit(t);
    }
}

static inline uint16_t imu_transport_max_sample_rate(imu_transport_t *t) {
    return imu_transport_prefix(t)->interface->max_sample_rate(t);
}

static inline void imu_transport_delay_us(imu_transport_t *t, uint32_t us) {
    if (t != (imu_transport_t *)0 && imu_transport_prefix(t)->interface->delay_us != (void *)0) {
        imu_transport_prefix(t)->interface->delay_us(t, us);
    }
}

#endif /* IMU_TRANSPORT_H */
