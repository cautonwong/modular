# The neutral RTOS interface: the host contract (`pal_rtos/rtos.h`), the assert
# contract (`pal_rtos/assert.h`), and the one its implementation needs to be shared
# by every port rather than copied into each.
edge_add_pal(rtos SOURCES src/assert.c DEPS edge_module pal_os)

add_subdirectory(freertos)
