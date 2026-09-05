cmake_minimum_required(VERSION 3.20)

option(HUNTSMAN_BUILD_FIRMWARE "Build the LPC5528 application image" OFF)
option(HUNTSMAN_USB_ONLY "Build USB bring-up without optical or lighting initialization" ON)

if(HUNTSMAN_BUILD_FIRMWARE)
    project(huntsman_v3_pro_mini_midi C ASM)
else()
    project(huntsman_v3_pro_mini_core_tests C)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(huntsman_core STATIC
    src/keyboard.c
    src/optical_scan.c
    src/updater_protocol.c
)
target_include_directories(huntsman_core PUBLIC include)
target_compile_options(huntsman_core PRIVATE -Wall -Wextra -Werror)

if(NOT HUNTSMAN_BUILD_FIRMWARE)
    enable_testing()
    add_executable(core_tests tests/test_core.c)
    target_link_libraries(core_tests PRIVATE huntsman_core)
    target_compile_options(core_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME core_tests COMMAND core_tests)
    return()
endif()

set(NXP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nxp)
set(NXP_DRIVERS ${NXP_ROOT}/core/drivers)
set(NXP_DEVICE ${NXP_ROOT}/devices/LPC5528)
set(NXP_USB ${NXP_ROOT}/usb)

add_executable(huntsman_firmware
    ${NXP_DEVICE}/gcc/startup_LPC5528.S
    ${NXP_DEVICE}/system_LPC5528.c
    ${NXP_DRIVERS}/common/fsl_common.c
    ${NXP_DRIVERS}/common/fsl_common_arm.c
    ${NXP_DRIVERS}/ctimer/fsl_ctimer.c
    ${NXP_ROOT}/devices/LPC55S69/drivers/fsl_clock.c
    ${NXP_ROOT}/devices/LPC55S69/drivers/fsl_power.c
    ${NXP_ROOT}/devices/LPC55S69/drivers/fsl_reset.c
    ${NXP_DRIVERS}/flexcomm/fsl_flexcomm.c
    ${NXP_DRIVERS}/flexcomm/i2c/fsl_i2c.c
    ${NXP_DRIVERS}/flexcomm/spi/fsl_spi.c
    ${NXP_DRIVERS}/lpc_gpio/fsl_gpio.c
    ${NXP_USB}/phy/usb_phy.c
    ${NXP_USB}/device/usb_device_dci.c
    ${NXP_USB}/device/usb_device_ch9.c
    ${NXP_USB}/device/usb_device_lpcip3511.c
    ${NXP_USB}/device/class/usb_device_class.c
    ${NXP_USB}/device/class/usb_device_hid.c
    ${NXP_USB}/device/class/usb_device_cdc_acm.c
    ${NXP_ROOT}/component/osa/fsl_os_abstraction_bm.c
    ${NXP_ROOT}/component/lists/fsl_component_generic_list.c
    src/board.c
    src/debug.c
    src/usb_composite.c
    src/usb_descriptors.c
    src/usb_errata.c
    src/usb_safe_memcpy.c
)
if(HUNTSMAN_USB_ONLY)
    target_sources(huntsman_firmware PRIVATE src/main_usb.c)
else()
    target_sources(huntsman_firmware PRIVATE src/main.c src/lighting.c src/optical_hw.c)
endif()
target_link_libraries(huntsman_firmware PRIVATE huntsman_core)

target_include_directories(huntsman_firmware PRIVATE
    include
    ${NXP_DEVICE}
    ${NXP_ROOT}/devices/periph
    ${NXP_ROOT}/devices/LPC55S69/drivers
    ${NXP_ROOT}/cmsis/Include
    ${NXP_DRIVERS}/common
    ${NXP_DRIVERS}/ctimer
    ${NXP_DRIVERS}/flexcomm
    ${NXP_DRIVERS}/flexcomm/i2c
    ${NXP_DRIVERS}/flexcomm/spi
    ${NXP_DRIVERS}/lpc_gpio
    ${NXP_USB}/include
    ${NXP_USB}/phy
    ${NXP_USB}/device
    ${NXP_USB}/device/class
    ${NXP_ROOT}/component/osa
    ${NXP_ROOT}/component/osa/config
    ${NXP_ROOT}/component/lists
)

target_compile_definitions(huntsman_firmware PRIVATE
    CPU_LPC5528JBD100
    __STARTUP_CLEAR_BSS
    __START=main
    SDK_DEBUGCONSOLE=0
)

set(MCU_FLAGS
    -mcpu=cortex-m33
    -mthumb
    # USB SRAM (0x40100000) is Device memory. GCC must not merge adjacent
    # byte fields into unaligned halfword/word accesses in NXP USB state.
    -mno-unaligned-access
    -mfpu=fpv5-sp-d16
    -mfloat-abi=hard
)
target_compile_options(huntsman_core PRIVATE ${MCU_FLAGS} -ffunction-sections -fdata-sections)
target_compile_options(huntsman_firmware PRIVATE
    ${MCU_FLAGS}
    -ffunction-sections
    -fdata-sections
    -fno-common
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Werror=implicit-function-declaration
)

target_link_options(huntsman_firmware PRIVATE
    ${MCU_FLAGS}
    -nostartfiles
    -Wl,--gc-sections
    # Observe EP0 status completion without modifying the vendor stack.
    -Wl,--wrap=USB_DeviceNotificationTrigger
    # Prebuilt newlib memcpy has unaligned halfword tails despite MCU_FLAGS.
    -Wl,--wrap=memcpy
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/huntsman_firmware.map
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/lpc5528_application.ld
)
target_link_libraries(huntsman_firmware PRIVATE c gcc nosys)

set_target_properties(huntsman_firmware PROPERTIES SUFFIX .elf)
set_property(TARGET huntsman_firmware APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/linker/lpc5528_application.ld)

# Explicit, offline-only target; optional Python dependencies are not needed
# to build firmware. This target never opens a USB device or runs the updater.
add_custom_target(audit-usb
    COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
            python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_usb_arm.py
            $<TARGET_FILE:huntsman_firmware>
    COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
            python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_usb_startup_arm.py
            $<TARGET_FILE:huntsman_firmware>
    COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
            python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_usb_chirp_arm.py
            $<TARGET_FILE:huntsman_firmware>
    DEPENDS huntsman_firmware
    USES_TERMINAL
    VERBATIM
)

add_custom_command(TARGET huntsman_firmware POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary --gap-fill 0xFF --pad-to 0x20020000
            $<TARGET_FILE:huntsman_firmware> ${CMAKE_CURRENT_BINARY_DIR}/huntsman_firmware.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:huntsman_firmware> ${CMAKE_CURRENT_BINARY_DIR}/huntsman_firmware.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:huntsman_firmware>
    COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
            python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_image.py
            --elf $<TARGET_FILE:huntsman_firmware>
            --bin ${CMAKE_CURRENT_BINARY_DIR}/huntsman_firmware.bin
    VERBATIM
)
