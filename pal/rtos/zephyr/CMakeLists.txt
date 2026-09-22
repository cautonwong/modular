# Zephyr RTOS PAL implementation (Issue #82)
edge_add_pal(rtos_zephyr RAW SOURCES src/zephyr_rtos.c src/zephyr_pal.c
    DEPS edge_module pal_os pal_rtos)
