# The IMU application layer: the register access and device interfaces, the drivers behind them, and -
# for the one part that has a vendor driver of its own - that driver as it came.

# Bosch's BMI160 driver, vendored whole from the reference's imu/BMI160_driver. It is not this
# repository's code and is not linted as if it were: warnings are off for this target alone, so that
# the port's own warnings-as-errors stay meaningful for everything it wrote itself.
add_library(bmi160_vendor STATIC vendor/bmi160/bmi160.c)
target_include_directories(bmi160_vendor PUBLIC vendor/bmi160)
if(NOT MSVC)
    target_compile_options(bmi160_vendor PRIVATE -w)
endif()

# xioTechnologies' Fusion, vendored whole from the reference's imu/Fusion. The same treatment, for the
# same reason. Its Examples directory is left out: it builds for other platforms and is not source this
# project's firmware could use, which is the only part of it that is not carried.
add_library(fusion_vendor STATIC vendor/Fusion/FusionAhrs.c vendor/Fusion/FusionBias.c
                               vendor/Fusion/FusionCompass.c)
target_include_directories(fusion_vendor PUBLIC vendor/Fusion)
if(NOT MSVC)
    target_compile_options(fusion_vendor PRIVATE -w)
endif()

edge_add_infra(imu
    SOURCES
        src/bmi160_wrapper.c
        src/icm20948.c
        src/imu.c
        src/lsm6ds3.c
        src/lsm6dsv32x.c
        src/mpu9150.c
    DEPS
        edge_module
)
target_link_libraries(infra_imu PUBLIC m bmi160_vendor fusion_vendor)
