#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "rle/rle.h"

static void test_rle_basic_decompression(void **state) {
    (void)state;

    /* Encoded: 3 pixels background (0x0000), 2 pixels foreground (0xFFFF), 4 pixels background
     * (0x0000) */
    const uint8_t rle_data[] = {3, 2, 4};
    rle_decoder_t decoder;
    rle_decoder_init(&decoder, rle_data, sizeof(rle_data), 0xFFFFu, 0x0000u);

    uint8_t out_buf[18]; /* 9 pixels * 2 bytes = 18 bytes */
    size_t written = 0;
    assert_int_equal(rle_decoder_decode_next(&decoder, out_buf, sizeof(out_buf), &written),
                     EDGE_OK);
    assert_int_equal(written, 18);

    /* Pixels 0, 1, 2: 0x0000 */
    assert_int_equal(out_buf[0], 0x00);
    assert_int_equal(out_buf[1], 0x00);
    assert_int_equal(out_buf[2], 0x00);
    assert_int_equal(out_buf[3], 0x00);
    assert_int_equal(out_buf[4], 0x00);
    assert_int_equal(out_buf[5], 0x00);

    /* Pixels 3, 4: 0xFFFF */
    assert_int_equal(out_buf[6], 0xFF);
    assert_int_equal(out_buf[7], 0xFF);
    assert_int_equal(out_buf[8], 0xFF);
    assert_int_equal(out_buf[9], 0xFF);

    /* Pixels 5, 6, 7, 8: 0x0000 */
    assert_int_equal(out_buf[10], 0x00);
    assert_int_equal(out_buf[11], 0x00);
    assert_int_equal(out_buf[16], 0x00);
    assert_int_equal(out_buf[17], 0x00);

    /* Subsequent decode returns EOF */
    assert_int_equal(rle_decoder_decode_next(&decoder, out_buf, sizeof(out_buf), &written),
                     EDGE_EOF);
}

static void test_rle_chunked_decompression(void **state) {
    (void)state;

    /* Encoded: 5 pixels bg (0x1234), 5 pixels fg (0x5678) */
    const uint8_t rle_data[] = {5, 5};
    rle_decoder_t decoder;
    rle_decoder_init(&decoder, rle_data, sizeof(rle_data), 0x5678u, 0x1234u);

    /* Decode in 3 chunks of 8 bytes (4 pixels) */
    uint8_t chunk[8];
    size_t written = 0;

    /* Chunk 1: 4 pixels bg */
    assert_int_equal(rle_decoder_decode_next(&decoder, chunk, sizeof(chunk), &written), EDGE_OK);
    assert_int_equal(written, 8);
    assert_int_equal(chunk[0], 0x12);
    assert_int_equal(chunk[1], 0x34);

    /* Chunk 2: 1 pixel bg, 3 pixels fg */
    assert_int_equal(rle_decoder_decode_next(&decoder, chunk, sizeof(chunk), &written), EDGE_OK);
    assert_int_equal(written, 8);
    assert_int_equal(chunk[0], 0x12);
    assert_int_equal(chunk[1], 0x34);
    assert_int_equal(chunk[2], 0x56);
    assert_int_equal(chunk[3], 0x78);

    /* Chunk 3: remaining 2 pixels fg */
    assert_int_equal(rle_decoder_decode_next(&decoder, chunk, sizeof(chunk), &written), EDGE_OK);
    assert_int_equal(written, 4);
    assert_int_equal(chunk[0], 0x56);
    assert_int_equal(chunk[1], 0x78);

    /* Chunk 4: EOF */
    assert_int_equal(rle_decoder_decode_next(&decoder, chunk, sizeof(chunk), &written), EDGE_EOF);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_rle_basic_decompression),
        cmocka_unit_test(test_rle_chunked_decompression),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
