# The neutral RTOS interface (edge_pal_port_t, task entry, assert hook). Header
# package only -- an implementation such as freertos/ provides the code.
edge_add_pal(rtos HEADERS DEPS edge_module pal_os)

add_subdirectory(freertos)
