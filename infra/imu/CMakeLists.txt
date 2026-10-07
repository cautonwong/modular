edge_add_infra(imu
    SOURCES
        src/icm20948.c
        src/imu.c
        src/lsm6ds3.c
        src/lsm6dsv32x.c
        src/mpu9150.c
    DEPS
        edge_module
)
target_link_libraries(infra_imu PUBLIC m)
