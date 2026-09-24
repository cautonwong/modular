edge_add_app(pas
    SOURCES
        src/pas.c
    DEPS
        edge_module
)
target_link_libraries(app_pas PUBLIC m)
