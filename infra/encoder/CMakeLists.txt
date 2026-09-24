edge_add_infra(encoder
    SOURCES
        src/encoder.c
    DEPS
        edge_module
)
if(NOT WIN32)
    target_link_libraries(infra_encoder PUBLIC m)
endif()

