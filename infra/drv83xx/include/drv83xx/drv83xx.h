#ifndef INFRA_DRV83XX_H
#define INFRA_DRV83XX_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DRV8301 Registers */
#define DRV8301_REG_STAT1 0x00
#define DRV8301_REG_STAT2 0x01
#define DRV8301_REG_CTRL1 0x02
#define DRV8301_REG_CTRL2 0x03

/* DRV8301 Fault Bits */
#define DRV8301_FAULT_FETLC_OC (1u << 0)
#define DRV8301_FAULT_FETHC_OC (1u << 1)
#define DRV8301_FAULT_FETLB_OC (1u << 2)
#define DRV8301_FAULT_FETHB_OC (1u << 3)
#define DRV8301_FAULT_FETLA_OC (1u << 4)
#define DRV8301_FAULT_FETHA_OC (1u << 5)
#define DRV8301_FAULT_OTW (1u << 6)
#define DRV8301_FAULT_OTSD (1u << 7)
#define DRV8301_FAULT_PVDD_UV (1u << 8)
#define DRV8301_FAULT_GVDD_UV (1u << 9)
#define DRV8301_FAULT_FAULT (1u << 10)

/* DRV8323 Registers */
#define DRV8323_REG_STAT0 0x00
#define DRV8323_REG_STAT1 0x01
#define DRV8323_REG_CTRL_GATE 0x02
#define DRV8323_REG_CTRL_OCP 0x05
#define DRV8323_REG_CTRL_CSA 0x06

typedef edge_status_t (*drv83xx_spi_transfer_fn)(void *ctx, uint16_t tx_val, uint16_t *rx_val);

typedef struct drv8301 {
    drv83xx_spi_transfer_fn spi_transfer;
    void *spi_ctx;
    uint16_t ctrl1_val;
    uint16_t ctrl2_val;
} drv8301_t;

typedef struct drv8323 {
    drv83xx_spi_transfer_fn spi_transfer;
    void *spi_ctx;
    uint16_t gate_ctrl;
    uint16_t ocp_ctrl;
    uint16_t csa_ctrl;
} drv8323_t;

/* DRV8301 API */
void drv8301_construct(drv8301_t *self, drv83xx_spi_transfer_fn spi_transfer, void *spi_ctx);
edge_status_t drv8301_init(drv8301_t *self, uint8_t pwm_mode, uint8_t ocp_mode, uint8_t gain);
edge_status_t drv8301_read_reg(drv8301_t *self, uint8_t reg, uint16_t *val);
edge_status_t drv8301_write_reg(drv8301_t *self, uint8_t reg, uint16_t val);
edge_status_t drv8301_read_faults(drv8301_t *self, uint16_t *faults);

/* DRV8323 API */
void drv8323_construct(drv8323_t *self, drv83xx_spi_transfer_fn spi_transfer, void *spi_ctx);
edge_status_t drv8323_init(drv8323_t *self, uint8_t pwm_mode, uint8_t ocp_mode, uint8_t csa_gain);
edge_status_t drv8323_read_reg(drv8323_t *self, uint8_t reg, uint16_t *val);
edge_status_t drv8323_write_reg(drv8323_t *self, uint8_t reg, uint16_t val);
edge_status_t drv8323_read_faults(drv8323_t *self, uint16_t *faults);

#ifdef __cplusplus
}
#endif

#endif /* INFRA_DRV83XX_H */
