set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc CACHE FILEPATH "ARM GCC compiler")
set(CMAKE_OBJCOPY arm-none-eabi-objcopy CACHE FILEPATH "ARM objcopy")
set(CMAKE_SIZE arm-none-eabi-size CACHE FILEPATH "ARM size")

# No executable can run on the build host; CMake must use a static-library
# try_compile for compiler/toolchain detection.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
