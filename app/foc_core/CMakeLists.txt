# FOC motor control application module
edge_add_app(foc_core SOURCES src/foc_core.c src/foc_math.c DEPS edge_module)
if(NOT WIN32)
    target_link_libraries(app_foc_core PUBLIC m)
endif()
