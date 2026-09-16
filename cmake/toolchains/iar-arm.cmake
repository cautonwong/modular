# IAR (iccarm) toolchain for the ARM firmware targets.
#
# Usage:
#   export IAR_TOOLCHAIN_PATH=/opt/iarsystems/arm/bin   # or add iccarm to PATH
#   cmake -S . -B build-iar -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/iar-arm.cmake \
#     -DEDGE_MODULE_BUILD_ARM_CORTEX_M4=ON \
#     -DEDGE_MODULE_BUILD_TESTS=OFF -DEDGE_MODULE_BUILD_EXAMPLE=OFF
#
# The CMake build remains the single source of truth (D25); IAR project files
# are only used for IDE debugging. This toolchain is provided as the dual-
# toolchain path but is not exercised in CI because iccarm requires a license.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(DEFINED ENV{IAR_TOOLCHAIN_PATH})
    set(_iar_bin "$ENV{IAR_TOOLCHAIN_PATH}")
else()
    set(_iar_bin "")
endif()

find_program(CMAKE_C_COMPILER NAMES iccarm HINTS "${_iar_bin}" REQUIRED)
find_program(IAR_ASM_COMPILER NAMES iasmarm HINTS "${_iar_bin}")
find_program(IAR_LINKER NAMES ilinkarm HINTS "${_iar_bin}")

# CMake cannot run an IAR-linked executable on the host; detect via static libs.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
