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

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_drv8301_init_and_registers),
        cmocka_unit_test(test_drv8323_init_and_registers),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
