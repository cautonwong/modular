edge_add_app(ppm
    SOURCES
        src/ppm.c
    DEPS
        edge_module
)
target_link_libraries(app_ppm PUBLIC m)
