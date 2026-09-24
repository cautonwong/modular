edge_add_infra(vesc_buffer
    SOURCES
        src/buffer.c
    DEPS
        edge_module
)
if(NOT WIN32)
    target_link_libraries(infra_vesc_buffer PUBLIC m)
endif()

