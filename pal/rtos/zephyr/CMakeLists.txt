# Zephyr RTOS PAL (Issue #82). West fetches Zephyr, not FetchContent, so the pin is
# recorded here and the smoke job checks it against the revision west resolved.
set(EDGE_MODULE_ZEPHYR_VERSION "v4.4.0" CACHE STRING
    "Zephyr version this port is written against (for humans)")
set(EDGE_MODULE_ZEPHYR_COMMIT "684c9e8f32e4373a21098559f748f06915f950c9"
    CACHE STRING "Zephyr commit - the immutable pin (see ci/dependencies.json)")

# Zephyr's own build compiles this port (`zephyr/CMakeLists.txt`); nothing in ours
# links it, and without Zephyr its sources do not compile at all. Gated like the
# FreeRTOS port, so a host or bare-metal build gets **no target** instead of a fake
# one - which is what the port's `#error` would otherwise turn into a build failure
# for every image that never asked for Zephyr.
if(NOT DEFINED ZEPHYR_BASE)
    return()
endif()

edge_add_pal(rtos_zephyr RAW SOURCES src/zephyr_rtos.c src/zephyr_pal.c
    DEPS edge_module pal_os pal_rtos)
