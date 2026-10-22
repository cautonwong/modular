#include "imu_internal.h"

/*
 * What stays out of the headers (#258): construction, and the accessors for the
 * fields a driver's `configure` rewrites. The hot calls - configure, sample read,
 * register access - are `static inline` in the public headers through the prefix
 * view, so an extra call is not on the sampling path.
 */

void imu_transport_construct(imu_transport_t *t, const imu_transport_interface_t *interface,
                             void *self) {
    if (t == (imu_transport_t *)0) {
        return;
    }
    *imu_transport_state(t) = (struct imu_transport_internal){
        .interface = interface,
        .self = self,
    };
}

void imu_device_construct(imu_device_t *dev, const imu_device_interface_t *interface,
                          imu_transport_t *transport, void *priv) {
    if (dev == (imu_device_t *)0) {
        return;
    }
    *imu_device_state(dev) = (struct imu_device_internal){
        .interface = interface,
        .transport = transport,
        .priv = priv,
    };
}

const char *imu_device_variant(const imu_device_t *dev) {
    return dev != (const imu_device_t *)0 ? imu_device_state_const(dev)->variant : (const char *)0;
}

uint8_t imu_device_address(const imu_device_t *dev) {
    return dev != (const imu_device_t *)0 ? imu_device_state_const(dev)->dev_addr : 0u;
}

uint16_t imu_device_sample_rate(const imu_device_t *dev) {
    return dev != (const imu_device_t *)0 ? imu_device_state_const(dev)->sample_rate_hz : 0u;
}

void imu_device_set_sample_rate(imu_device_t *dev, uint16_t hz) {
    if (dev != (imu_device_t *)0) {
        imu_device_state(dev)->sample_rate_hz = hz;
    }
}

bool imu_device_uses_drdy(const imu_device_t *dev) {
    return dev != (const imu_device_t *)0 ? imu_device_state_const(dev)->use_drdy : false;
}

void imu_device_set_use_drdy(imu_device_t *dev, bool use_drdy) {
    if (dev != (imu_device_t *)0) {
        imu_device_state(dev)->use_drdy = use_drdy;
    }
}

void *imu_device_priv(const imu_device_t *dev) {
    return dev != (const imu_device_t *)0 ? imu_device_state_const(dev)->priv : (void *)0;
}
