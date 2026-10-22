#include "imu_internal.h"

/*
 * The public surface of the two opaque handles (#258): construction, the calls a
 * caller makes, and the accessors the fields became. Everything here is a function
 * rather than a `static inline` in the header, because an inline body would need the
 * struct the headers no longer define.
 */

void imu_transport_construct(imu_transport_t *t, const imu_transport_interface_t *interface,
                             void *self) {
    if (t == (imu_transport_t *)0) {
        return;
    }
    *imu_transport_state(t) = (struct imu_transport_internal){.interface = interface, .self = self};
}

const imu_transport_interface_t *imu_transport_interface(const imu_transport_t *t) {
    return t != (const imu_transport_t *)0 ? imu_transport_state_const(t)->interface : (const imu_transport_interface_t *)0;
}

void *imu_transport_self(const imu_transport_t *t) {
    return t != (const imu_transport_t *)0 ? imu_transport_state_const(t)->self : (void *)0;
}

bool imu_transport_read_reg(imu_transport_t *t, uint8_t dev_addr, uint8_t reg, uint8_t *rx,
                            size_t len) {
    if (t == (imu_transport_t *)0) {
        return false;
    }
    return imu_transport_state(t)->interface->read_reg(t, dev_addr, reg, rx, len);
}

bool imu_transport_write_reg(imu_transport_t *t, uint8_t dev_addr, uint8_t reg, const uint8_t *tx,
                             size_t len) {
    if (t == (imu_transport_t *)0) {
        return false;
    }
    return imu_transport_state(t)->interface->write_reg(t, dev_addr, reg, tx, len);
}

void imu_transport_recover(imu_transport_t *t) {
    if (t == (imu_transport_t *)0) {
        return;
    }
    if (imu_transport_state(t)->interface->recover != (void *)0) {
        imu_transport_state(t)->interface->recover(t);
    }
}

void imu_transport_deinit(imu_transport_t *t) {
    if (t == (imu_transport_t *)0) {
        return;
    }
    if (imu_transport_state(t)->interface->deinit != (void *)0) {
        imu_transport_state(t)->interface->deinit(t);
    }
}

uint16_t imu_transport_max_sample_rate(imu_transport_t *t) {
    if (t == (imu_transport_t *)0) {
        return 0u;
    }
    return imu_transport_state(t)->interface->max_sample_rate(t);
}

void imu_transport_delay_us(imu_transport_t *t, uint32_t us) {
    if (t == (imu_transport_t *)0) {
        return;
    }
    if (imu_transport_state(t)->interface->delay_us != (void *)0) {
        imu_transport_state(t)->interface->delay_us(t, us);
    }
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

bool imu_device_configure(imu_device_t *dev, imu_filter_t filter, bool use_mag) {
    return imu_device_state(dev)->interface->configure(dev, filter, use_mag);
}

bool imu_device_read_sample(imu_device_t *dev, float accel[3], float gyro[3], float mag[3]) {
    return imu_device_state(dev)->interface->read_sample(dev, accel, gyro, mag);
}

void imu_device_on_read_fail(imu_device_t *dev) {
    if (imu_device_state(dev)->interface->on_read_fail != (void *)0) {
        imu_device_state(dev)->interface->on_read_fail(dev);
    }
}

void imu_device_enable_drdy_output(imu_device_t *dev, bool enable) {
    if (imu_device_state(dev)->interface->enable_drdy_output != (void *)0) {
        imu_device_state(dev)->interface->enable_drdy_output(dev, enable);
    }
}

const imu_device_interface_t *imu_device_interface(const imu_device_t *dev) {
    return dev != (const imu_device_t *)0 ? imu_device_state_const(dev)->interface : (const imu_device_interface_t *)0;
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
