/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>
/* clang-format on */

#include "drv83xx/drv83xx.h"
#include "edge/errors.h"

typedef struct mock_spi_ctx {
    uint16_t last_tx;
    uint16_t inject_rx;
    size_t transfer_count;
    uint16_t regs[16];
} mock_spi_ctx_t;

static edge_status_t mock_drv_spi_transfer(void *ctx, uint16_t tx_val, uint16_t *rx_val) {
    mock_spi_ctx_t *m = (mock_spi_ctx_t *)ctx;
    m->last_tx = tx_val;
    m->transfer_count++;

    uint8_t is_read = (tx_val >> 15) & 1u;
    uint8_t reg = (tx_val >> 11) & 0x0Fu;
    uint16_t data = tx_val & 0x07FFu;

    if (!is_read) {
        m->regs[reg] = data;
    }

    *rx_val = m->inject_rx != 0 ? m->inject_rx : m->regs[reg];
    return EDGE_OK;
}

static void test_drv8301_init_and_registers(void **state) {
    (void)state;
    mock_spi_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    drv8301_t drv;
    drv8301_construct(&drv, mock_drv_spi_transfer, &ctx);

    assert_int_equal(drv8301_init(&drv, 0, 1, 2), EDGE_OK);
    assert_true(ctx.transfer_count >= 2);

    /* Verify CTRL1 write: gain=2 (bits 7:6 = 2<<6 = 0x80), ocp=1 (bits 5:4 = 1<<4 = 0x10) */
    assert_int_equal(ctx.regs[DRV8301_REG_CTRL1], 0x0090u);

    /* Test fault reading */
    ctx.inject_rx = DRV8301_FAULT_FETHA_OC | DRV8301_FAULT_FAULT;
    uint16_t faults = 0;
    assert_int_equal(drv8301_read_faults(&drv, &faults), EDGE_OK);
    assert_true((faults & DRV8301_FAULT_FETHA_OC) != 0);
    assert_true((faults & DRV8301_FAULT_FAULT) != 0);
}

static void test_drv8323_init_and_registers(void **state) {
    (void)state;
    mock_spi_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    drv8323_t drv;
    drv8323_construct(&drv, mock_drv_spi_transfer, &ctx);

    assert_int_equal(drv8323_init(&drv, 0, 1, 3), EDGE_OK);
    assert_true(ctx.transfer_count >= 3);

    /* Verify CSA gain = 3 (bits 7:6 = 3<<6 = 0xC0) */
    assert_int_equal(ctx.regs[DRV8323_REG_CTRL_CSA], 0x00C0u);
}

/* A transfer that fails, for the paths where the driver has to report it rather than carry on. */
static edge_status_t failing_transfer(void *ctx, uint16_t tx_val, uint16_t *rx_val) {
    (void)ctx;
    (void)tx_val;
    *rx_val = 0u;
    return EDGE_EIO;
}

/*
 * The guards and the failing-transfer paths of both drivers. A null driver, a null register output
 * or a missing transfer function is refused rather than dereferenced, and a transfer that fails is
 * reported as such instead of being turned into a register value - which is the case that matters
 * on a real board, where a dead SPI bus must not read as a healthy gate driver.
 */
static void test_drv83xx_guards_and_transfer_failure(void **state) {
    (void)state;
    mock_spi_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    uint16_t val = 0u;

    /* Construct accepts a null self and a null transfer function; every use then refuses. */
    drv8301_construct(NULL, mock_drv_spi_transfer, &ctx);
    drv8323_construct(NULL, mock_drv_spi_transfer, &ctx);
    drv8301_t bare;
    drv8301_construct(&bare, NULL, &ctx);
    assert_int_equal(drv8301_init(&bare, 0u, 0u, 0u), EDGE_EINVAL);
    assert_int_equal(drv8301_read_reg(&bare, DRV8301_REG_STAT1, &val), EDGE_EINVAL);
    assert_int_equal(drv8301_write_reg(&bare, DRV8301_REG_CTRL1, 0u), EDGE_EINVAL);
    assert_int_equal(drv8301_read_faults(&bare, &val), EDGE_EINVAL);

    drv8301_t d1;
    drv8301_construct(&d1, mock_drv_spi_transfer, &ctx);
    assert_int_equal(drv8301_read_reg(NULL, DRV8301_REG_STAT1, &val), EDGE_EINVAL);
    assert_int_equal(drv8301_read_reg(&d1, DRV8301_REG_STAT1, NULL), EDGE_EINVAL);
    assert_int_equal(drv8301_write_reg(NULL, DRV8301_REG_CTRL1, 0u), EDGE_EINVAL);
    assert_int_equal(drv8301_init(NULL, 0u, 0u, 0u), EDGE_EINVAL);
    assert_int_equal(drv8301_read_faults(NULL, &val), EDGE_EINVAL);
    assert_int_equal(drv8301_read_faults(&d1, NULL), EDGE_EINVAL);

    /* A dead bus is reported, not converted into a plausible register value. */
    drv8301_t bad;
    drv8301_construct(&bad, failing_transfer, &ctx);
    assert_int_equal(drv8301_read_reg(&bad, DRV8301_REG_STAT1, &val), EDGE_EIO);
    assert_int_equal(drv8301_write_reg(&bad, DRV8301_REG_CTRL1, 0u), EDGE_EIO);
    assert_int_equal(drv8301_init(&bad, 0u, 0u, 0u), EDGE_EIO);
    assert_int_equal(drv8301_read_faults(&bad, &val), EDGE_EIO);

    drv8323_t d23;
    drv8323_construct(&d23, mock_drv_spi_transfer, &ctx);
    assert_int_equal(drv8323_read_reg(NULL, 0u, &val), EDGE_EINVAL);
    assert_int_equal(drv8323_read_reg(&d23, 0u, NULL), EDGE_EINVAL);
    assert_int_equal(drv8323_write_reg(NULL, 0u, 0u), EDGE_EINVAL);
    assert_int_equal(drv8323_init(NULL, 0u, 0u, 0u), EDGE_EINVAL);
    assert_int_equal(drv8323_read_faults(NULL, &val), EDGE_EINVAL);

    drv8323_t bad23;
    drv8323_construct(&bad23, failing_transfer, &ctx);
    assert_int_equal(drv8323_read_reg(&bad23, 0u, &val), EDGE_EIO);
    assert_int_equal(drv8323_init(&bad23, 0u, 0u, 0u), EDGE_EIO);
    assert_int_equal(drv8323_read_faults(&bad23, &val), EDGE_EIO);
}

int main(void) {

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_drv8301_init_and_registers),
        cmocka_unit_test(test_drv83xx_guards_and_transfer_failure),
        cmocka_unit_test(test_drv8323_init_and_registers),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
