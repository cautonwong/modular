# RIOT-OS RTOS PAL implementation (Issue #135 / RIOT integration)
edge_add_pal(rtos_riot RAW SOURCES src/riot_rtos.c src/riot_pal.c
    DEPS edge_module pal_os pal_rtos)
