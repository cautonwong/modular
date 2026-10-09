#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/desc.h"
#include "edge/product.h"

typedef struct mock_service {
    int init_count;
    int start_count;
    int stop_count;
    bool should_fail_init;
} mock_service_t;

static edge_status_t mock_service_init(void *instance, const void *deps) {
    (void)deps;
    mock_service_t *svc = (mock_service_t *)instance;
    if (svc->should_fail_init) {
        return EDGE_EIO;
    }
    svc->init_count++;
    return EDGE_OK;
}

static edge_status_t mock_service_start(void *instance) {
    mock_service_t *svc = (mock_service_t *)instance;
    svc->start_count++;
    return EDGE_OK;
}

static edge_status_t mock_service_stop(void *instance) {
    mock_service_t *svc = (mock_service_t *)instance;
    svc->stop_count++;
    return EDGE_OK;
}

static const edge_module_desc_t g_mock_desc = {
    .id = 0x5100u,
    .name = "mock_service",
    .instance_size = sizeof(mock_service_t),
    .dependencies = NULL,
    .dependency_count = 0,
    .init = mock_service_init,
    .start = mock_service_start,
    .stop = mock_service_stop,
    .suspend = NULL,
    .resume = NULL,
};

static void test_descriptor_lifecycle_basic(void **state) {
    (void)state;
    mock_service_t svc = {0};

    assert_int_equal(edge_module_desc_init(&g_mock_desc, &svc, NULL), EDGE_OK);
    assert_int_equal(svc.init_count, 1);

    assert_int_equal(edge_module_desc_start(&g_mock_desc, &svc), EDGE_OK);
    assert_int_equal(svc.start_count, 1);

    assert_int_equal(edge_module_desc_stop(&g_mock_desc, &svc), EDGE_OK);
    assert_int_equal(svc.stop_count, 1);
}

static void test_product_lifecycle_and_rollback(void **state) {
    (void)state;
    mock_service_t svc1 = {0};
    mock_service_t svc2 = {.should_fail_init = true};

    edge_product_module_t modules[] = {
        {.desc = &g_mock_desc, .instance = &svc1, .module = NULL, .deps = NULL},
        {.desc = &g_mock_desc, .instance = &svc2, .module = NULL, .deps = NULL},
    };

    edge_product_t product = {
        .name = "test_product",
        .modules = modules,
        .module_count = 2,
        .platform_init = NULL,
        .platform_deinit = NULL,
    };

    /* Init must fail because svc2 fails, and svc1 must be rolled back (stopped) */
    edge_status_t st = edge_product_init(&product);
    assert_int_equal(st, EDGE_EIO);
    assert_int_equal(svc1.init_count, 1);
    assert_int_equal(svc1.stop_count, 1);
    assert_int_equal(svc2.init_count, 0);
}

static void test_product_clean_shutdown(void **state) {
    (void)state;
    mock_service_t svc1 = {0};
    mock_service_t svc2 = {0};

    edge_product_module_t modules[] = {
        {.desc = &g_mock_desc, .instance = &svc1, .module = NULL, .deps = NULL},
        {.desc = &g_mock_desc, .instance = &svc2, .module = NULL, .deps = NULL},
    };

    edge_product_t product = {
        .name = "test_product_clean",
        .modules = modules,
        .module_count = 2,
        .platform_init = NULL,
        .platform_deinit = NULL,
    };

    assert_int_equal(edge_product_init(&product), EDGE_OK);
    assert_int_equal(svc1.init_count, 1);
    assert_int_equal(svc2.init_count, 1);

    edge_product_shutdown(&product);
    assert_int_equal(svc1.stop_count, 1);
    assert_int_equal(svc2.stop_count, 1);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_descriptor_lifecycle_basic),
        cmocka_unit_test(test_product_lifecycle_and_rollback),
        cmocka_unit_test(test_product_clean_shutdown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
