edge_add_app(motor_id
    SOURCES
        src/motor_id.c
    DEPS
        edge_module
)
target_link_libraries(app_motor_id PUBLIC m)
