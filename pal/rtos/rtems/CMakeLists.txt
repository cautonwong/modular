# RTEMS RTOS PAL implementation (Issue #135 / RTEMS integration)
edge_add_pal(rtos_rtems RAW SOURCES src/rtems_rtos.c src/rtems_pal.c
    DEPS edge_module pal_os pal_rtos)
