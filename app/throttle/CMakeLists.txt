edge_add_app(throttle
    SOURCES
        src/throttle.c
    DEPS
        edge_module
)
target_link_libraries(app_throttle PUBLIC m)
