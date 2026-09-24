edge_add_infra(imu
    SOURCES
        src/imu.c
    DEPS
        edge_module
)
target_link_libraries(infra_imu PUBLIC m)
