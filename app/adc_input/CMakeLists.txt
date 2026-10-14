edge_add_app(adc_input
    SOURCES
        src/adc_input.c
    DEPS
        edge_module
)
target_link_libraries(app_adc_input PUBLIC m)
