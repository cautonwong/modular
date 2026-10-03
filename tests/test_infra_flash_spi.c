#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "flash_spi/flash_spi.h"

static void test_flash_spi_format_cmd(void **state) {
    (void)state;
    uint8_t hdr[4] = {0};
    assert_int_equal(flash_spi_format_cmd_addr(FLASH_SPI_CMD_READ, 0x00123456u, hdr), EDGE_OK);
    assert_int_equal(hdr[0], FLASH_SPI_CMD_READ);
    assert_int_equal(hdr[1], 0x12);
    assert_int_equal(hdr[2], 0x34);
    assert_int_equal(hdr[3], 0x56);
}

static void test_flash_spi_unpack_jedec(void **state) {
    (void)state;
    // XT25F32B JEDEC ID: Manufacturer=0x0B, Type=0x40, Capacity=0x16 (4MB)
    uint8_t raw[3] = {0x0B, 0x40, 0x16};
    flash_spi_jedec_id_t id;
    assert_int_equal(flash_spi_unpack_jedec_id(raw, &id), EDGE_OK);
    assert_int_equal(id.manufacturer_id, 0x0B);
    assert_int_equal(id.memory_type, 0x40);
    assert_int_equal(id.capacity_id, 0x16);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_flash_spi_format_cmd),
        cmocka_unit_test(test_flash_spi_unpack_jedec),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
