# ==============================================================================
# boards/ch32l103_ob/board.cmake
#
# Board-specific build definitions, included by the top level CMakeLists.txt
# when BOARD selects this directory.
# ==============================================================================

# ------------------------------------------------------------------------------
# Clock tree: internal oscillator, no crystal fitted
#
#     HSI 8 MHz  ->  /2 (EXTEN_PLL_HSI_PRE, ch32fun sets it for this part)
#                ->  PLL x18  ->  SYSCLK = HCLK = 72 MHz, APB1 = HCLK/2 = 36 MHz
#
# 72 MHz is the ceiling of the HSI path: RCC_PLLMULL18 is the largest factor the
# part offers and ch32fun halves the HSI before the PLL, so 4 MHz x 18 is it.
# The USBFS kernel clock is then PLL / 1.5 = 48 MHz, which is why
# board_config.h sets BOARD_USB_PRESCALER (the 96 MHz crystal boards use / 2).
#
# HSI is +/-1 % over temperature.  That is fine for USB - the SIE resynchronises
# on every packet - but it is why a crystal board is preferable whenever CAN is
# involved, since CAN bit timing needs the tighter tolerance.
#
# HSE_VALUE is deliberately not set: nothing here derives from a crystal.
# ------------------------------------------------------------------------------
add_compile_definitions(
    FUNCONF_USE_HSI=1
    FUNCONF_USE_HSE=0
    FUNCONF_PLL_MULTIPLIER=18
    FUNCONF_SYSTEM_CORE_CLOCK=72000000
)

# ------------------------------------------------------------------------------
# Flash size
#
# CH32L103C8T6: 64 KB of code flash.  The bootloader owns the first 12 KB, so
# the application partition is 52 KB (13 whole DfuSe sectors) and the layout
# string the device reports is "@Internal Flash /0x08003000/13*004Kg".
# ------------------------------------------------------------------------------
add_compile_definitions(BOOT_FLASH_SIZE=65536)
