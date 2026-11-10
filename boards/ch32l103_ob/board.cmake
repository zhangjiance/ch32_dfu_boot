# ==============================================================================
# boards/ch32l103_ob/board.cmake
#
# Board-specific build definitions, included by the top level CMakeLists.txt
# when BOARD selects this directory.
# ==============================================================================

# ------------------------------------------------------------------------------
# Clock tree: internal oscillator, no crystal fitted
#
#     HSI 8 MHz  ->  PLL x12  ->  SYSCLK = HCLK = 96 MHz, APB1 = HCLK/2 = 48 MHz
#
# The QFN32 (CH32L103K8U6) pinout brings out neither OSC_IN/OSC_OUT nor NRST, so
# there is no way to fit a crystal: HSI is the only clock source and reset is
# power-on or software only.
#
# On the HSI path ch32fun sets EXTEN_PLL_HSI_PRE, which selects the HSI as the
# PLL input *without* dividing it, so the PLL sees the full 8 MHz.  x12 reaches
# the 96 MHz maximum, and the USBFS kernel clock is then PLL / 2 = 48 MHz (the
# chip port's default divider, so this board does not override it).
#
# HSE_VALUE is kept only as a harmless fallback equal to the HSI frequency, in
# case shared code still references it; HSE itself is disabled below.
# ------------------------------------------------------------------------------
add_compile_definitions(
    HSE_VALUE=8000000
    FUNCONF_USE_HSI=1
    FUNCONF_USE_HSE=0
    FUNCONF_PLL_MULTIPLIER=12
    FUNCONF_SYSTEM_CORE_CLOCK=96000000
)

# ------------------------------------------------------------------------------
# Flash size
#
# CH32L103K8U6: 64 KB of code flash, 20 KB SRAM.  The bootloader owns the first
# 12 KB, so the application partition is 52 KB (13 whole DfuSe sectors) and the
# DfuSe layout string the device reports is "@Internal Flash /0x08003000/13*004Kg".
# ------------------------------------------------------------------------------
add_compile_definitions(BOOT_FLASH_SIZE=65536)
