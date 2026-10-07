edge_add_infra(imu
    SOURCES
        src/imu.c
        src/mpu9150.c
    DEPS
        edge_module
)
target_link_libraries(infra_imu PUBLIC m)
