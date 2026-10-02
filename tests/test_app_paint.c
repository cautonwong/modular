#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/modules.h"
#include "paint/paint.h"

typedef struct mock_paint {
    int16_t last_x;
    int16_t last_y;
    uint16_t last_color;
    uint16_t vibrate_ms;
} mock_paint_t;

static edge_status_t mock_fill_rect(void *self, int16_t x, int16_t y, uint16_t w, uint16_t h,
                                    uint16_t rgb565) {
    (void)w;
    (void)h;
    mock_paint_t *m = (mock_paint_t *)self;
    m->last_x = x;
    m->last_y = y;
    m->last_color = rgb565;
    return EDGE_OK;
}

static edge_status_t mock_clear(void *self, uint16_t color) {
    mock_paint_t *m = (mock_paint_t *)self;
    m->last_color = color;
    return EDGE_OK;
}

static edge_status_t mock_vibrate(void *self, uint16_t duration_ms) {
    mock_paint_t *m = (mock_paint_t *)self;
    m->vibrate_ms = duration_ms;
    return EDGE_OK;
}

static void test_paint_palette_and_strokes(void **state) {
    (void)state;
    mock_paint_t mock = {0};
    paint_display_if_t disp = {
        .self = &mock,
        .fill_rect = mock_fill_rect,
        .clear = mock_clear,
    };
    paint_motor_if_t motor = {
        .self = &mock,
        .vibrate = mock_vibrate,
    };

    paint_app_t app;
    paint_construct(&app, EDGE_MOD_PAINT, 20u, &disp, &motor);
    assert_int_equal(paint_init(&app), EDGE_OK);

    assert_int_equal(paint_get_color_index(&app), 2); /* White */
    assert_int_equal(paint_get_color_rgb565(&app), 0xFFFFu);

    /* Draw point at (100, 100) */
    assert_int_equal(paint_draw_point(&app, 100, 100), EDGE_OK);
    assert_int_equal(mock.last_x, 95);
    assert_int_equal(mock.last_y, 95);
    assert_int_equal(mock.last_color, 0xFFFFu);
    assert_int_equal(paint_get_stroke_count(&app), 1);

    /* Cycle color -> Red */
    paint_cycle_color(&app);
    assert_int_equal(paint_get_color_index(&app), 3);
    assert_int_equal(paint_get_color_rgb565(&app), 0xF800u);
    assert_int_equal(mock.vibrate_ms, 35u);

    /* Clear canvas */
    assert_int_equal(paint_clear_canvas(&app), EDGE_OK);
    assert_int_equal(paint_get_stroke_count(&app), 0);
    assert_int_equal(mock.last_color, 0x0000u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_paint_palette_and_strokes),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
