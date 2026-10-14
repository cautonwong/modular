edge_add_app(dpv
    SOURCES
        src/dpv.c
    DEPS
        edge_module
)
target_link_libraries(app_dpv PUBLIC m)
