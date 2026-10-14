edge_add_app(skypuff
    SOURCES
        src/skypuff.c
    DEPS
        edge_module
)
target_link_libraries(app_skypuff PUBLIC m)
