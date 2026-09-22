# CMakeLists.txt for Zephyr-hosted meter product (Issue #82)
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meter_zephyr)

target_sources(app PRIVATE
    main.c
    glue.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
