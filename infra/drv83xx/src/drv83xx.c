#include "drv83xx/drv83xx.h"
#include <string.h>

/* DRV8301 Implementation */
void drv8301_construct(drv8301_t *self, drv83xx_spi_transfer_fn spi_transfer, void *spi_ctx) {
    if (self == (void *)0) {
        return;
    }
    memset(self, 0, sizeof(*self));
    self->spi_transfer = spi_transfer;
    self->spi_ctx = spi_ctx;
}

edge_status_t drv8301_read_reg(drv8301_t *self, uint8_t reg, uint16_t *val) {
    if (self == (void *)0 || self->spi_transfer == (void *)0 || val == (void *)0) {
        return EDGE_EINVAL;
    }

    uint16_t tx = (uint16_t)(1u << 15) | (uint16_t)((reg & 0x0Fu) << 11);
    uint16_t dummy = 0;
    edge_status_t status = self->spi_transfer(self->spi_ctx, tx, &dummy);
    if (status != EDGE_OK) {
        return status;
    }

    /* DRV8301 returns read result on subsequent SPI cycle */
    status = self->spi_transfer(self->spi_ctx, tx, val);
    if (status == EDGE_OK) {
        *val = *val & 0x07FFu;
    }
    return status;
}

edge_status_t drv8301_write_reg(drv8301_t *self, uint8_t reg, uint16_t val) {
    if (self == (void *)0 || self->spi_transfer == (void *)0) {
        return EDGE_EINVAL;
    }

    uint16_t tx = (uint16_t)((reg & 0x0Fu) << 11) | (val & 0x07FFu);
    uint16_t rx = 0;
    return self->spi_transfer(self->spi_ctx, tx, &rx);
}

edge_status_t drv8301_init(drv8301_t *self, uint8_t pwm_mode, uint8_t ocp_mode, uint8_t gain) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    /* CTRL1: PWM mode, OCP mode, current shunt gain */
    uint16_t ctrl1 = ((uint16_t)(pwm_mode & 0x01u) << 3) | ((uint16_t)(ocp_mode & 0x03u) << 4) |
                     ((uint16_t)(gain & 0x03u) << 6);
    self->ctrl1_val = ctrl1;

    edge_status_t status = drv8301_write_reg(self, DRV8301_REG_CTRL1, ctrl1);
    if (status != EDGE_OK) {
        return status;
    }

    /* CTRL2: default configuration */
    self->ctrl2_val = 0x0000u;
    return drv8301_write_reg(self, DRV8301_REG_CTRL2, self->ctrl2_val);
}

edge_status_t drv8301_read_faults(drv8301_t *self, uint16_t *faults) {
    if (self == (void *)0 || faults == (void *)0) {
        return EDGE_EINVAL;
    }

    uint16_t stat1 = 0;
    edge_status_t status = drv8301_read_reg(self, DRV8301_REG_STAT1, &stat1);
    if (status != EDGE_OK) {
        return status;
    }

    *faults = stat1;
    return EDGE_OK;
}

/* DRV8323 Implementation */
void drv8323_construct(drv8323_t *self, drv83xx_spi_transfer_fn spi_transfer, void *spi_ctx) {
    if (self == (void *)0) {
        return;
    }
    memset(self, 0, sizeof(*self));
    self->spi_transfer = spi_transfer;
    self->spi_ctx = spi_ctx;
}

edge_status_t drv8323_read_reg(drv8323_t *self, uint8_t reg, uint16_t *val) {
    if (self == (void *)0 || self->spi_transfer == (void *)0 || val == (void *)0) {
        return EDGE_EINVAL;
    }

    uint16_t tx = (uint16_t)(1u << 15) | (uint16_t)((reg & 0x0Fu) << 11);
    uint16_t dummy = 0;
    edge_status_t status = self->spi_transfer(self->spi_ctx, tx, &dummy);
    if (status != EDGE_OK) {
        return status;
    }

    status = self->spi_transfer(self->spi_ctx, tx, val);
    if (status == EDGE_OK) {
        *val = *val & 0x07FFu;
    }
    return status;
}

edge_status_t drv8323_write_reg(drv8323_t *self, uint8_t reg, uint16_t val) {
    if (self == (void *)0 || self->spi_transfer == (void *)0) {
        return EDGE_EINVAL;
    }

    uint16_t tx = (uint16_t)((reg & 0x0Fu) << 11) | (val & 0x07FFu);
    uint16_t rx = 0;
    return self->spi_transfer(self->spi_ctx, tx, &rx);
}

edge_status_t drv8323_init(drv8323_t *self, uint8_t pwm_mode, uint8_t ocp_mode, uint8_t csa_gain) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    self->gate_ctrl = (uint16_t)((pwm_mode & 0x03u) << 5);
    self->ocp_ctrl = (uint16_t)((ocp_mode & 0x03u) << 4);
    self->csa_ctrl = (uint16_t)((csa_gain & 0x03u) << 6);

    edge_status_t status = drv8323_write_reg(self, DRV8323_REG_CTRL_GATE, self->gate_ctrl);
    if (status != EDGE_OK) {
        return status;
    }

    status = drv8323_write_reg(self, DRV8323_REG_CTRL_OCP, self->ocp_ctrl);
    if (status != EDGE_OK) {
        return status;
    }

    return drv8323_write_reg(self, DRV8323_REG_CTRL_CSA, self->csa_ctrl);
}

edge_status_t drv8323_read_faults(drv8323_t *self, uint16_t *faults) {
    if (self == (void *)0 || faults == (void *)0) {
        return EDGE_EINVAL;
    }

    uint16_t stat0 = 0;
    edge_status_t status = drv8323_read_reg(self, DRV8323_REG_STAT0, &stat0);
    if (status != EDGE_OK) {
        return status;
    }

    *faults = stat0;
    return EDGE_OK;
}
