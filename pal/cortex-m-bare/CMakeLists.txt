# Bare-metal Cortex-M PAL (D46/D85).
#
# Architecture binding (D49/D85, defect #128): the port is built natively for an
# ARM toolchain, or off target *only* for the host unit tests, where it compiles
# the opt-in host fallback behind EDGE_PAL_CORTEX_M_HOST_TEST. A non-ARM cross
# build (RISC-V, AArch64) gets no target at all instead of a silently linked fake
# clock: nothing can link it, and the source refuses to compile without either
# the ARM target or the explicit host macro.
if(CMAKE_C_COMPILER MATCHES "arm-none-eabi" OR CMAKE_C_COMPILER MATCHES "iccarm")
    # Native Cortex-M: the real register implementation.
elseif(NOT CMAKE_CROSSCOMPILING)
    # Host build: unit tests and the host smoke products. The macro is defined on
    # the library because the library is compiled once for the whole build; the
    # test cannot re-enable a branch that was already compiled out.
    set(_edge_cortex_m_host_test TRUE)
else()
    message(STATUS "pal/cortex-m-bare: skipping, ${CMAKE_C_COMPILER} is not a Cortex-M toolchain")
    return()
endif()

edge_add_pal(cortex_m_bare SOURCES src/pal_cortex_m.c DEPS edge_module)

if(_edge_cortex_m_host_test)
    target_compile_definitions(pal_cortex_m_bare PRIVATE EDGE_PAL_CORTEX_M_HOST_TEST=1)
endif()
