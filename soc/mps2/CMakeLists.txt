# SoC support package for the ARM MPS2 (AN385/AN386): register map, the kernel
# configuration fragment a product's FreeRTOSConfig.h includes, and the
# SoC-level helpers.
#
# SOURCES, not HEADERS: a support package for a real SoC carries code (register
# drivers, a vendor HAL, SoC-level init - see the target layout in docs/adr.md
# section 24), and a header-only package would leave that shape untested until
# the first real SoC arrived (#147).
edge_add_soc(mps2 SOURCES src/soc_mps2.c DEPS edge_module)
