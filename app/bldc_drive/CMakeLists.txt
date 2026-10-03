edge_add_app(bldc_drive
    SOURCES
        src/bldc_commutation.c
        src/bldc_drive.c
    DEPS
        edge_module
)
target_link_libraries(app_bldc_drive PUBLIC m)
