# Native Linux PAL
edge_add_pal(linux SOURCES src/pal_linux.c DEPS edge_module pal_os)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR (NOT CMAKE_CROSSCOMPILING AND NOT WIN32))
    find_package(Threads REQUIRED)
    target_link_libraries(pal_linux PRIVATE Threads::Threads)
endif()
