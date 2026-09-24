edge_add_infra(digital_filter
    SOURCES
        src/digital_filter.c
    DEPS
        edge_module
)
target_link_libraries(infra_digital_filter PUBLIC m)
