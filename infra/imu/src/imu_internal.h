#ifndef IMU_INTERNAL_H
#define IMU_INTERNAL_H

/*
 * The two IMU aggregate types, kept out of the public headers (#258).
 *
 * `imu_device_t` and `imu_transport_t` are opaque, aligned storage blocks in
 * `imu/device.h` and `imu/transport.h`; this is what lives in them. The public
 * structs published `dev_addr`, `variant`, `sample_rate_hz` and `use_drdy`, which a
 * driver's `configure` rewrites - state a caller could reach into and desynchronise
 * from the device it describes.
 *
 * The types are still returned by value from their factories, so the storage block
 * (not a forward declaration) is what keeps `imu_device_t dev = lsm6ds3_device(...)`
 * working while the fields become unreachable.
 */
#include "imu/device.h"
#include "imu/transport.h"

#include <stddef.h>
#include <stdint.h>

struct imu_transport_internal {
    const imu_transport_interface_t *interface;
    void *self;
};

struct imu_device_internal {
    const imu_device_interface_t *interface;
    imu_transport_t *transport;
    const char *variant;
    uint8_t dev_addr;
    uint16_t sample_rate_hz;
    bool use_drdy;
    void *priv;
};

/* The inline wrappers in the public headers read the first two members through
 * `imu_device_prefix_t` / `imu_transport_prefix_t`; these keep that promise. */
_Static_assert(offsetof(struct imu_device_internal, interface) ==
                   offsetof(imu_device_prefix_t, interface),
               "the inline wrappers read `interface` through the prefix view");
_Static_assert(offsetof(struct imu_device_internal, transport) ==
                   offsetof(imu_device_prefix_t, transport),
               "the prefix view must keep `transport` in place");
_Static_assert(offsetof(struct imu_transport_internal, interface) ==
                   offsetof(imu_transport_prefix_t, interface),
               "the inline wrappers read `interface` through the prefix view");
_Static_assert(offsetof(struct imu_transport_internal, self) ==
                   offsetof(imu_transport_prefix_t, self),
               "the prefix view must keep `self` in place");

/* Exact margins, measured on both ABIs, so a new field fails the build with a
 * message that says what to re-measure rather than quietly shrinking the slack. */
_Static_assert(IMU_DEVICE_STORAGE_SIZE - sizeof(struct imu_device_internal) ==
                   IMU_DEVICE_STORAGE_MARGIN,
               "imu_device_t storage drifted: re-measure IMU_DEVICE_STORAGE_SIZE and _MARGIN");
_Static_assert(_Alignof(struct imu_device_internal) <= IMU_DEVICE_STORAGE_ALIGN,
               "IMU_DEVICE_STORAGE_ALIGN is stale: the storage would be under-aligned");
_Static_assert(
    IMU_TRANSPORT_STORAGE_SIZE - sizeof(struct imu_transport_internal) ==
        IMU_TRANSPORT_STORAGE_MARGIN,
    "imu_transport_t storage drifted: it is exactly two pointers, so a field is a redesign");
_Static_assert(_Alignof(struct imu_transport_internal) <= IMU_TRANSPORT_STORAGE_ALIGN,
               "IMU_TRANSPORT_STORAGE_ALIGN is stale: the storage would be under-aligned");

static inline struct imu_device_internal *imu_device_state(const imu_device_t *handle) {
    return (struct imu_device_internal *)(void *)(uintptr_t)handle;
}

static inline struct imu_transport_internal *imu_transport_state(const imu_transport_t *handle) {
    return (struct imu_transport_internal *)(void *)(uintptr_t)handle;
}

static inline const struct imu_device_internal *imu_device_state_const(const imu_device_t *handle) {
    return (const struct imu_device_internal *)(const void *)(uintptr_t)handle;
}

static inline const struct imu_transport_internal *
imu_transport_state_const(const imu_transport_t *handle) {
    return (const struct imu_transport_internal *)(const void *)(uintptr_t)handle;
}

#endif /* IMU_INTERNAL_H */
