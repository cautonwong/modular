# RISC-V 32 bare-metal toolchain (SiFive/`virt` QEMU, RV32IMC).
#
# Usage:
#   cmake -S . -B build-rv -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv-elf.cmake \
#     -DEDGE_MODULE_BUILD_RISCV32_QEMU=ON \
#     -DEDGE_MODULE_BUILD_TESTS=OFF -DEDGE_MODULE_BUILD_EXAMPLE=OFF

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)

set(CMAKE_C_COMPILER riscv64-unknown-elf-gcc CACHE FILEPATH "RISC-V bare-metal C compiler")
set(CMAKE_OBJCOPY riscv64-unknown-elf-objcopy CACHE FILEPATH "RISC-V objcopy")
set(CMAKE_SIZE riscv64-unknown-elf-size CACHE FILEPATH "RISC-V size")

# RV32IMC / ilp32 applies to every object, including static libraries.
set(CMAKE_C_FLAGS_INIT "-march=rv32imc_zicsr -mabi=ilp32 -mcmodel=medany -fstack-usage")
set(CMAKE_ASM_FLAGS_INIT "-march=rv32imc_zicsr -mabi=ilp32 -mcmodel=medany")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-march=rv32imc_zicsr -mabi=ilp32 -mcmodel=medany")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
