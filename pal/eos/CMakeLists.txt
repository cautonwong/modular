# EOS: the first-party runtime (D46/D85).
#
# A kernel *module* inside the `pal` layer, sibling to pal/rtos/freertos: no new
# layer and no new registry, because pal already carries architecture x RTOS
# families and a new layer would need CMake/registry plumbing to hold nothing
# extra (D88).
edge_add_pal(eos SOURCES src/eos.c src/eos_rtos.c DEPS edge_module pal_os pal_rtos)
