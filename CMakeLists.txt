cmake_minimum_required(VERSION 3.22)

# ==============================================================================
# ch32_dfu_boot - CH32 USB DFU bootloader (CH32V30x USBHS / CH32L103 USBFS)
#
# Layering (kept deliberately decoupled so new boards/chips are additive):
#   shared/            boot <-> app contract (partition, hand-shake)
#   boards/<board>/    board_config.h + board.c + board.cmake (self-contained)
#   port/<chip>/       flash + hand-shake + USB controller, core ISA, memory map
#   user/              chip/board agnostic firmware (entry, descriptors, DFU)
#
# ch32fun supplies startup, clock and printf; USB goes through CherryUSB with
# the chip port's device controller (USBHS on the CH32V30x, USBFS on the
# CH32L103); the boot core stays free of chip and board specifics.
# ==============================================================================

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_PROJECT_NAME ch32_dfu_boot)

# CherryUSB device stack: the DFU class backs the DfuSe interface.
set(CONFIG_CHERRYUSB_DEVICE 1)
set(CONFIG_CHERRYUSB_DEVICE_DFU 1)

set(CHERRYUSB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party_components/CherryUSB"
    CACHE PATH "CherryUSB submodule root")

# ------------------------------------------------------------------------------
# Peripheral library: ch32fun (https://github.com/cnlohr/ch32fun)
#
# No vendor HAL anywhere: ch32fun.c supplies the startup, the clock tree and the
# printf transport, and everything else is register access in board.c and the
# chip port.  Kept in third_party_components/ch32fun (submodule) or point
# -DCH32FUN_ROOT=... at another checkout.
# ------------------------------------------------------------------------------
set(CH32FUN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party_components/ch32fun"
    CACHE PATH "ch32fun checkout (the submodule, or a path of your own)")

if(NOT EXISTS "${CH32FUN_ROOT}/ch32fun/ch32fun.c")
    message(FATAL_ERROR
        "ch32fun not found at ${CH32FUN_ROOT}.  Run "
        "git submodule update --init --recursive, or point -DCH32FUN_ROOT=... at it.")
endif()

# ------------------------------------------------------------------------------
# Board / chip port selection
# ------------------------------------------------------------------------------
set(BOARD "ch32v30x_ob" CACHE STRING "Target board directory under boards/")
set(BOARD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD}")

file(GLOB _board_cfgs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/boards" "${CMAKE_CURRENT_SOURCE_DIR}/boards/*/board_config.h")
set(_available_boards "")
foreach(_cfg ${_board_cfgs})
    get_filename_component(_dir "${_cfg}" DIRECTORY)
    list(APPEND _available_boards "${_dir}")
endforeach()

if(NOT EXISTS "${BOARD_DIR}/board_config.h")
    message(FATAL_ERROR "Unknown BOARD '${BOARD}'. Available boards: ${_available_boards}")
endif()

# Board Support Package.  A board directory is self-contained - board_config.h
# (pins/features), board.h and board.c (primitives) - so copying one under
# boards/ and selecting it with -DBOARD=<name> is all it takes to reuse it.
if(NOT EXISTS "${BOARD_DIR}/board.c")
    message(FATAL_ERROR "Board '${BOARD}' has no board.c (expected boards/${BOARD}/board.c)")
endif()
set(BOARD_SRC "${BOARD_DIR}/board.c")

# Optional per-board compile definitions (e.g. -DBOOT_FLASH_SIZE=...).
if(EXISTS "${BOARD_DIR}/board.cmake")
    include("${BOARD_DIR}/board.cmake")
endif()

set(CHIP_PORT "ch32v30x" CACHE STRING "Chip port directory under port/")
set(CHIP_PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/port/${CHIP_PORT}")
if(NOT EXISTS "${CHIP_PORT_DIR}")
    message(FATAL_ERROR "Unknown CHIP_PORT '${CHIP_PORT}'")
endif()

# Chip port: flash, cross-reset hand-shake, USB controller glue, register level.
set(_port_srcs
    ${CHIP_PORT_DIR}/boot_flash_${CHIP_PORT}.c
    ${CHIP_PORT_DIR}/boot_trigger_${CHIP_PORT}.c
    ${CHIP_PORT_DIR}/boot_usb_${CHIP_PORT}.c)

if(NOT EXISTS ${CHERRYUSB_ROOT}/class/dfu/usbd_dfu.c)
    message(FATAL_ERROR "CherryUSB submodule missing; run: git submodule update --init --recursive")
endif()

# Per-chip build settings: core ISA string, chip name, USB device controller
# source/include dir, ESIG unique-ID address, linker script.  Hard-wiring the
# CH32V30x values here is what made the V30x assumptions leak into the shared
# firmware; everything chip specific now lives in port/<chip>/port.cmake.
set(CHIP_MARCH "rv32imacxw" CACHE STRING "RISC-V ISA string of the selected chip port")
if(EXISTS "${CHIP_PORT_DIR}/port.cmake")
    include("${CHIP_PORT_DIR}/port.cmake")
endif()

if(NOT CHIP_USB_SRCS)
    message(FATAL_ERROR "Chip port '${CHIP_PORT}' has no CHIP_USB_SRCS (expected in port/${CHIP_PORT}/port.cmake)")
endif()

message(STATUS "Board: ${BOARD}   Chip port: ${CHIP_PORT} (${BOOT_CHIP_NAME}, -march=${CHIP_MARCH})")
message(STATUS "  board src: ${BOARD_SRC}")
message(STATUS "  usb ctrl:  ${CHIP_USB_SRCS}")
message(STATUS "  linker:    ${LINKER_SCRIPT}")
message(STATUS "  ch32fun:   ${CH32FUN_ROOT}")

# Toolchain before project() (sets the riscv-wch-elf- compilers).
include(cmake/riscv.cmake)
include("${CHERRYUSB_ROOT}/cherryusb.cmake")

# CherryUSB master has no usable CH32V30x USBHS port: port/wch/usbhs targets a
# different USBHS IP (CH32V205/V4x7/CH32X305/CH58x) with an incompatible
# register map, and CH32V30x is only covered under USBFS.  The chip port
# therefore uses the validated USBHS device driver from our fork
# (third_party_components/CherryUSB/port/wch/ch32v30x/usb_dc_ch32v30x.c, from
# CherryUSB v1.6.0's port/ch32/ch32hs).  The CH32L103 needs none of that: its
# USBFS is covered by CherryUSB's own port/wch/usbfs.  The core/classes always
# come from the submodule.

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
enable_language(C ASM)

project(${CMAKE_PROJECT_NAME})

set(PROJECT_TARGET ${CMAKE_PROJECT_NAME}.elf)
if(NOT DEFINED LINKER_SCRIPT)
    message(FATAL_ERROR "Chip port '${CHIP_PORT}' sets no LINKER_SCRIPT (expected in port/${CHIP_PORT}/port.cmake)")
endif()

add_link_options(
    -T ${LINKER_SCRIPT}
    -Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.map
)

add_executable(${PROJECT_TARGET})

# ------------------------------------------------------------------------------
# Firmware sources
# ------------------------------------------------------------------------------
target_sources(${PROJECT_TARGET} PRIVATE
    ${cherryusb_srcs}
    # ch32fun: startup, clock tree, SysTick and printf
    ${CH32FUN_ROOT}/ch32fun/ch32fun.c
    # chip port: flash, hand-shake, USB clock + low-level init
    ${_port_srcs}
    # USB device controller of the selected chip port
    ${CHIP_USB_SRCS}
    # board primitives
    ${BOARD_SRC}
    # chip/board agnostic firmware
    ${CMAKE_CURRENT_SOURCE_DIR}/user/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/user/boot_entry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/user/dfu_desc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/user/dfu_port.c
)

target_include_directories(${PROJECT_TARGET} PRIVATE
    ${cherryusb_incs}
    ${CHIP_USB_INCS}
    ${CH32FUN_ROOT}/ch32fun
    ${CMAKE_CURRENT_SOURCE_DIR}/shared
    ${CMAKE_CURRENT_SOURCE_DIR}/port
    ${BOARD_DIR}
    ${CHIP_PORT_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/user
)

# BOOT_PRINTF logging is Debug-only: a Release build logs nothing, and keeping
# the formatter out of the image is most of the size budget.  The chip name is
# a chip-port property (BOOT_CHIP_NAME in port/<chip>/port.cmake) and reaches
# the banner and the USB strings from the command line, so the shared firmware
# never names a part.
target_compile_definitions(${PROJECT_TARGET} PRIVATE
    BOOT_CHIP_NAME="${BOOT_CHIP_NAME}"
    $<$<CONFIG:Debug>:BOOT_ENABLE_LOG=1>
)

# ch32fun.c is one big translation unit that also carries the whole mini libc
# (printf/snprintf/sprintf/strstr/setjmp/...), all of it as weak symbols.  In a
# -flto build LTO keeps those weak definitions alive and merges them into a
# single .text, where --gc-sections can no longer drop them, so ~1.3 KB of code
# the bootloader never calls is linked anyway.  Compiling just this file without
# LTO gives every function its own section again.
set_source_files_properties(${CH32FUN_ROOT}/ch32fun/ch32fun.c
    PROPERTIES COMPILE_OPTIONS "-fno-lto")

add_custom_command(TARGET ${PROJECT_TARGET} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -Oihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf> ${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -Obinary $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf> ${CMAKE_PROJECT_NAME}.bin
)
