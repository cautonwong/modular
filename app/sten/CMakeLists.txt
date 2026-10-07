edge_add_app(sten
    SOURCES
        src/sten.c
    DEPS
        edge_module
)
target_link_libraries(app_sten PUBLIC m)
