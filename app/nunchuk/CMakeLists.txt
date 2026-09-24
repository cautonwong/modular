edge_add_app(nunchuk
    SOURCES
        src/nunchuk.c
    DEPS
        edge_module
)
target_link_libraries(app_nunchuk PUBLIC m)
