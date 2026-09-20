cmake_minimum_required(VERSION 3.20)
project(edge_modular VERSION 0.5.0 LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CTest)
option(EDGE_MODULE_BUILD_TESTS "Build unit tests" ${BUILD_TESTING})
option(EDGE_MODULE_BUILD_EXAMPLE "Build example product" ON)
option(EDGE_MODULE_ENABLE_WERROR "Treat compiler warnings as errors" ON)
option(EDGE_MODULE_ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)
option(EDGE_MODULE_ENABLE_COVERAGE "Enable GCC coverage" OFF)
option(EDGE_MODULE_BUILD_ARM_CORTEX_M0 "Build Cortex-M0 cross target" OFF)
option(EDGE_MODULE_BUILD_ARM_CORTEX_M4 "Build Cortex-M4 cross target" OFF)
option(EDGE_MODULE_BUILD_ARM_CORTEX_M4_QEMU "Build Cortex-M4 MPS2 AN386 target" OFF)
option(EDGE_MODULE_ARM_QEMU_SEMIHOSTING "Enable ARM semihosting exit for QEMU smoke tests" OFF)
option(EDGE_MODULE_BUILD_RISCV32_QEMU "Build RISC-V 32 QEMU (virt) target" OFF)
option(EDGE_MODULE_BUILD_FREERTOS_QEMU "Build FreeRTOS-hosted Cortex-M4 QEMU target" OFF)
set(EDGE_MODULE_FREERTOS_KERNEL_TAG "V11.1.0" CACHE STRING "FreeRTOS-Kernel git tag")

# When targeting bare-metal ARM, every compiled object (including static
# libraries) must use the same core and Thumb mode. Selecting it globally avoids
# linking ARM-mode library objects into a Thumb firmware image.
if(EDGE_MODULE_BUILD_ARM_CORTEX_M0)
    set(EDGE_ARM_CPU cortex-m0)
elseif(EDGE_MODULE_BUILD_ARM_CORTEX_M4 OR EDGE_MODULE_BUILD_ARM_CORTEX_M4_QEMU OR EDGE_MODULE_BUILD_FREERTOS_QEMU)
    set(EDGE_ARM_CPU cortex-m4)
endif()
if(DEFINED EDGE_ARM_CPU)
    add_compile_options(-mcpu=${EDGE_ARM_CPU} -mthumb -mno-unaligned-access -fstack-usage)
    add_link_options(-mcpu=${EDGE_ARM_CPU} -mthumb)
endif()

function(edge_enable_quality target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(EDGE_MODULE_ENABLE_WERROR)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
    if(EDGE_MODULE_ENABLE_SANITIZERS)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
    if(EDGE_MODULE_ENABLE_COVERAGE)
        target_compile_options(${target} PRIVATE --coverage -O0 -g)
        target_link_options(${target} PRIVATE --coverage)
    endif()
endfunction()

# How a module target is built lives in cmake/EdgeTargets.cmake (D88); which
# modules exist lives in each area directory's own CMakeLists.txt.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/EdgeTargets.cmake)

# Discover the modules of an area. Every directory under it must declare itself,
# because silent discovery would allow "the target exists but is never built",
# which is harder to notice than a merge conflict. Order inside an area is
# sorted so the build is reproducible.
function(edge_add_area area)
    file(GLOB entries LIST_DIRECTORIES true RELATIVE
        "${CMAKE_CURRENT_SOURCE_DIR}/${area}" "${CMAKE_CURRENT_SOURCE_DIR}/${area}/*")
    list(SORT entries)
    foreach(entry IN LISTS entries)
        if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${area}/${entry}")
            if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${area}/${entry}/CMakeLists.txt")
                message(FATAL_ERROR
                    "${area}/${entry}/ has no CMakeLists.txt: every area directory declares its "
                    "own target (D88); see docs/how-to/add-driver.md")
            endif()
            add_subdirectory("${area}/${entry}")
        endif()
    endforeach()
endfunction()

add_subdirectory(edge_module)
edge_add_area(sys)
edge_add_area(soc)
edge_add_area(infra)
edge_add_area(pal)
edge_add_area(board)
edge_add_area(app)

# Legal family x board combinations (D39). An unknown family/board fails above;
# a known-but-unlisted pair is a product composition error and fails here.
set(EDGE_LEGAL_FAMILY_BOARD "example:example;meter:example;meter:mps2;meter:riscv_virt")

function(edge_add_product name)
    cmake_parse_arguments(P "" "family;board" "apps;infra" ${ARGN})
    if(NOT P_family OR NOT P_board OR NOT P_apps)
        message(FATAL_ERROR "edge_add_product(${name}): family, board and apps are required")
    endif()
    if(NOT DEFINED EDGE_SYS_TARGET_${P_family})
        message(FATAL_ERROR "edge_add_product(${name}): unknown family '${P_family}'")
    endif()
    if(NOT DEFINED EDGE_BOARD_TARGET_${P_board})
        message(FATAL_ERROR "edge_add_product(${name}): unknown board '${P_board}'")
    endif()
    if(NOT "${P_family}:${P_board}" IN_LIST EDGE_LEGAL_FAMILY_BOARD)
        message(FATAL_ERROR "edge_add_product(${name}): illegal family/board combination '${P_family}' + '${P_board}'")
    endif()

    # D86: a product is registered exactly once and is permanently bound to one
    # board. The binding is recorded here so firmware targets can only reuse it --
    # they take a product name, never a board, and cannot override this.
    if(DEFINED EDGE_PRODUCT_${name}_BOARD)
        message(FATAL_ERROR
            "edge_add_product(${name}): product already registered (board "
            "'${EDGE_PRODUCT_${name}_BOARD}'); a product name may only be registered once")
    endif()
    set(EDGE_PRODUCT_${name}_BOARD "${P_board}" CACHE INTERNAL "Product ${name} board binding")
    set(EDGE_PRODUCT_${name}_FAMILY "${P_family}" CACHE INTERNAL "Product ${name} family binding")

    set(dep_targets ${EDGE_SYS_TARGET_${P_family}} ${EDGE_BOARD_TARGET_${P_board}})
    foreach(app IN LISTS P_apps)
        if(NOT DEFINED EDGE_APP_TARGET_${app})
            message(FATAL_ERROR "edge_add_product(${name}): unknown app '${app}'")
        endif()
        list(APPEND dep_targets ${EDGE_APP_TARGET_${app}})
    endforeach()
    foreach(infra IN LISTS P_infra)
        if(NOT DEFINED EDGE_INFRA_TARGET_${infra})
            message(FATAL_ERROR "edge_add_product(${name}): unknown infra '${infra}'")
        endif()
        list(APPEND dep_targets ${EDGE_INFRA_TARGET_${infra}})
    endforeach()

    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/product/${name}/main.c")
        message(FATAL_ERROR "product/${name}/main.c is missing")
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/product/${name}/glue.c")
        message(FATAL_ERROR "product/${name}/glue.c is missing")
    endif()

    # Record dependencies so edge_add_arm_firmware() can reuse the same product
    # definition without re-parsing the composition.
    set(EDGE_PRODUCT_${name}_TARGETS "${dep_targets}" CACHE INTERNAL "Product ${name} dependencies")
    if(NOT EDGE_MODULE_BUILD_EXAMPLE)
        return()
    endif()

    add_executable(${name}
        product/${name}/main.c
        product/${name}/glue.c
    )
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/product/${name}
        ${CMAKE_CURRENT_SOURCE_DIR}/edge_module/include
    )
    target_link_libraries(${name} PRIVATE edge_module ${dep_targets})
    edge_enable_quality(${name})
endfunction()

# T7b neutrality fixture: one minimal product, parameterised over board + runner
# (and app). The app is never modified for a variant.
if(NOT CMAKE_CROSSCOMPILING)
    find_package(Threads REQUIRED)
endif()
function(edge_add_minimal_variant name board runner app)
    if(NOT DEFINED EDGE_BOARD_TARGET_${board})
        message(FATAL_ERROR "edge_add_minimal_variant(${name}): unknown board '${board}'")
    endif()
    if(NOT DEFINED EDGE_APP_TARGET_${app})
        message(FATAL_ERROR "edge_add_minimal_variant(${name}): unknown app '${app}'")
    endif()
    if(runner STREQUAL "baremetal")
        set(main_src tests/integration/minimal_product/main_baremetal.c)
    elseif(runner STREQUAL "thread")
        set(main_src tests/integration/minimal_product/main_thread.c)
    else()
        message(FATAL_ERROR "edge_add_minimal_variant(${name}): unknown runner '${runner}'")
    endif()

    add_executable(${name} ${main_src} tests/integration/minimal_product/glue.c)
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/minimal_product
    )
    target_link_libraries(${name} PRIVATE
        edge_module
        ${EDGE_SYS_TARGET_example}
        ${EDGE_BOARD_TARGET_${board}}
        ${EDGE_APP_TARGET_${app}}
        ${EDGE_INFRA_TARGET_flash}
    )
    if(runner STREQUAL "thread")
        target_link_libraries(${name} PRIVATE Threads::Threads)
    endif()
    edge_enable_quality(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "host")
endfunction()

edge_add_product(example family example board example infra flash apps dlt645)
edge_add_product(meter_host family meter board example infra flash gpio apps dlt645 relay)
edge_add_product(meter_mps2 family meter board mps2 infra flash gpio apps dlt645 relay)
edge_add_product(meter_gateway_host family meter board example infra flash uart apps dlt645 modbus_slave meter_core)

# Host neutrality matrix: board example x {baremetal, thread} runner, same app.
if(NOT CMAKE_CROSSCOMPILING)
    edge_add_minimal_variant(minimal_host_baremetal example baremetal dlt645)
    edge_add_minimal_variant(minimal_host_thread example thread dlt645)
endif()
if(EDGE_MODULE_BUILD_RISCV32_QEMU)
    edge_add_product(riscv_meter family meter board riscv_virt infra flash apps dlt645)
endif()
if(EDGE_MODULE_BUILD_FREERTOS_QEMU)
    edge_add_product(meter_mps2_freertos family meter board mps2 infra flash apps dlt645)
endif()

# Negative validation for the combinator: configuring with
# -DEDGE_MODULE_NEGATIVE_CASE=<family|board|app|infra> must fail. Consumers live
# in tests/negative_products.cmake so the positive product list stays clean.
set(EDGE_MODULE_NEGATIVE_CASE "" CACHE STRING "Force an invalid product combination")
if(EDGE_MODULE_NEGATIVE_CASE)
    include(${CMAKE_CURRENT_SOURCE_DIR}/tests/negative_products.cmake)
endif()

function(edge_add_arm_firmware name product cpu linker_script)
    if(NOT CMAKE_C_COMPILER MATCHES "arm-none-eabi-gcc$")
        message(FATAL_ERROR "${name} requires arm-none-eabi-gcc toolchain")
    endif()
    if(NOT DEFINED EDGE_PRODUCT_${product}_TARGETS)
        message(FATAL_ERROR "${name}: unknown product '${product}'")
    endif()
    if(NOT DEFINED EDGE_PRODUCT_${product}_BOARD)
        message(FATAL_ERROR "${name}: product '${product}' has no registered board binding (D86)")
    endif()

    add_executable(${name}
        product/${product}/main.c
        product/${product}/glue.c
        .github/arm/startup.c
    )
    target_link_libraries(${name} PRIVATE edge_module ${EDGE_PRODUCT_${product}_TARGETS})
    target_compile_definitions(${name} PRIVATE EDGE_TARGET_ARM32=1)
    if(EDGE_MODULE_ARM_QEMU_SEMIHOSTING)
        target_compile_definitions(${name} PRIVATE EDGE_QEMU_SEMIHOSTING=1)
    endif()
    if(ARGC GREATER 4)
        target_compile_definitions(${name} PRIVATE EDGE_BOARD_TIMER_ISR=${ARGV4})
    endif()
    target_compile_options(${name} PRIVATE
        -mcpu=${cpu} -mthumb -mno-unaligned-access
        -ffreestanding -fdata-sections -ffunction-sections
        -fno-common -Os
    )
    target_link_options(${name} PRIVATE
        -mcpu=${cpu} -mthumb
        -T${CMAKE_CURRENT_SOURCE_DIR}/${linker_script}
        -nostartfiles
        --specs=nosys.specs
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${name}.map
    )
    set_target_properties(${name} PROPERTIES OUTPUT_NAME ${name})
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}> ${CMAKE_CURRENT_BINARY_DIR}/${name}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}>
        VERBATIM
    )
endfunction()

if(EDGE_MODULE_BUILD_ARM_CORTEX_M0)
    edge_add_arm_firmware(example_cortex_m0 example cortex-m0 .github/arm/cortex-m0.ld)
endif()

if(EDGE_MODULE_BUILD_ARM_CORTEX_M4)
    edge_add_arm_firmware(example_cortex_m4 example cortex-m4 .github/arm/cortex-m4.ld)
endif()

if(EDGE_MODULE_BUILD_ARM_CORTEX_M4_QEMU)
    edge_add_arm_firmware(meter_mps2_fw meter_mps2 cortex-m4 .github/arm/cortex-m4-qemu.ld board_mps2_irq_timer0)
endif()

function(edge_add_riscv_firmware name product linker_script)
    if(NOT CMAKE_C_COMPILER MATCHES "riscv.*-gcc$")
        message(FATAL_ERROR "${name} requires a RISC-V gcc toolchain")
    endif()
    if(NOT DEFINED EDGE_PRODUCT_${product}_TARGETS)
        message(FATAL_ERROR "${name}: unknown product '${product}'")
    endif()
    if(NOT DEFINED EDGE_PRODUCT_${product}_BOARD)
        message(FATAL_ERROR "${name}: product '${product}' has no registered board binding (D86)")
    endif()

    add_executable(${name}
        product/${product}/main.c
        product/${product}/glue.c
        .github/riscv/startup.S
        .github/riscv/support.c
    )
    target_link_libraries(${name} PRIVATE edge_module ${EDGE_PRODUCT_${product}_TARGETS} -lgcc)
    target_compile_definitions(${name} PRIVATE EDGE_TARGET_RISCV32=1)
    target_compile_options(${name} PRIVATE
        -ffreestanding -fdata-sections -ffunction-sections -fno-common -Os
    )
    target_link_options(${name} PRIVATE
        -nostdlib
        -T${CMAKE_CURRENT_SOURCE_DIR}/${linker_script}
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${name}.map
    )
    set_target_properties(${name} PROPERTIES OUTPUT_NAME ${name})
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}> ${CMAKE_CURRENT_BINARY_DIR}/${name}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}>
        VERBATIM
    )
endfunction()

if(EDGE_MODULE_BUILD_RISCV32_QEMU)
    edge_add_riscv_firmware(riscv_meter riscv_meter .github/riscv/riscv-virt.ld)
endif()

function(edge_add_arm_freertos_firmware name product)
    if(NOT CMAKE_C_COMPILER MATCHES "arm-none-eabi-gcc$")
        message(FATAL_ERROR "${name} requires arm-none-eabi-gcc toolchain")
    endif()
    if(NOT DEFINED EDGE_PRODUCT_${product}_TARGETS)
        message(FATAL_ERROR "${name}: unknown product '${product}'")
    endif()
    if(NOT DEFINED EDGE_PRODUCT_${product}_BOARD)
        message(FATAL_ERROR "${name}: product '${product}' has no registered board binding (D86)")
    endif()

    # D87: one compiled kernel serves exactly one product configuration. Letting
    # two products share a kernel compiled against different configs would be an
    # ODR/ABI hazard; per-product kernel targets are deferred until a second RTOS
    # product actually exists.
    if(DEFINED EDGE_FREERTOS_CONFIG_PRODUCT AND NOT EDGE_FREERTOS_CONFIG_PRODUCT STREQUAL "${product}")
        message(FATAL_ERROR
            "${name}: the FreeRTOS kernel is already configured for '${EDGE_FREERTOS_CONFIG_PRODUCT}'; "
            "a second FreeRTOS product needs its own kernel target (deferred, see D87)")
    endif()
    set(EDGE_FREERTOS_CONFIG_PRODUCT "${product}" CACHE INTERNAL "Product owning the FreeRTOS config")

    # The product owns include/FreeRTOSConfig.h and its board supplies the
    # board/soc include roots, so the kernel and the PAL are compiled against the
    # same configuration instance. pal/ ships no FreeRTOSConfig.h (pal -> product
    # is forbidden by the layer rule).
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/product/${product}/include/FreeRTOSConfig.h")
        message(FATAL_ERROR
            "${name}: product '${product}' must provide include/FreeRTOSConfig.h (D87)")
    endif()
    set(_fw_board ${EDGE_PRODUCT_${product}_BOARD})
    get_target_property(_fw_board_includes ${EDGE_BOARD_TARGET_${_fw_board}}
                        INTERFACE_INCLUDE_DIRECTORIES)
    foreach(_tgt freertos_kernel pal_rtos_freertos)
        target_include_directories(${_tgt} PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/product/${product}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/pal/rtos/include
            ${_fw_board_includes}
        )
    endforeach()

    add_executable(${name}
        product/${product}/main.c
        product/${product}/glue.c
        .github/arm/startup.c
    )
    target_link_libraries(${name} PRIVATE
        edge_module ${EDGE_PRODUCT_${product}_TARGETS} pal_rtos_freertos freertos_kernel
    )
    target_compile_definitions(${name} PRIVATE
        EDGE_TARGET_ARM32=1
        EDGE_QEMU_SEMIHOSTING=1
        EDGE_SVC_HANDLER=vPortSVCHandler
        EDGE_PENDSV_HANDLER=xPortPendSVHandler
        EDGE_SYSTICK_HANDLER=xPortSysTickHandler
    )
    # The board IRQ handler (same contract as edge_add_arm_firmware): without it
    # the timer vector falls back to default_handler and the first interrupt
    # parks the CPU in a WFI loop.
    if(ARGC GREATER 2)
        target_compile_definitions(${name} PRIVATE EDGE_BOARD_TIMER_ISR=${ARGV2})
    endif()
    target_compile_options(${name} PRIVATE
        -mcpu=cortex-m4 -mthumb -mno-unaligned-access -mfloat-abi=soft
        -ffreestanding -fdata-sections -ffunction-sections -fno-common -Os
    )
    target_link_options(${name} PRIVATE
        -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
        -T${CMAKE_CURRENT_SOURCE_DIR}/.github/arm/cortex-m4-qemu.ld
        -nostartfiles --specs=nosys.specs
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${name}.map
    )
    set_target_properties(${name} PROPERTIES OUTPUT_NAME ${name})
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}> ${CMAKE_CURRENT_BINARY_DIR}/${name}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}>
        VERBATIM
    )
endfunction()

if(EDGE_MODULE_BUILD_FREERTOS_QEMU)
    edge_add_arm_freertos_firmware(meter_mps2_freertos_fw meter_mps2_freertos board_mps2_irq_timer0)
endif()

if(EDGE_MODULE_BUILD_TESTS)
    # Every host test names the modules it exercises, so adding an app or a
    # driver never edits a shared list (D88).
    add_subdirectory(tests)
endif()
