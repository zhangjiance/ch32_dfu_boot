/*
 * board_config.h - "ch32l103_ob"
 *
 * Board Support Package for the "OB" target board, a CH32L103K8U6 in a QFN32
 * package: a full-speed USB device port, an RGB LED, a SWD/JTAG header and a
 * USART1 pin header.  The package brings out neither OSC_IN/OSC_OUT nor NRST,
 * so the part runs from its internal oscillator and resets by power-on or
 * software only (see board.cmake for the clock tree).
 *
 * Each board lives in its own, self-contained folder under boards/:
 *     boards/<board>/board_config.h   pins / polarity / features (this file)
 *     boards/<board>/board.h          board primitive interface (copy as is)
 *     boards/<board>/board.c          board primitive implementation (copy as is)
 *     boards/<board>/board.cmake      clock tree + flash size
 *
 * Adding a board should be nothing more than copying this folder, editing the
 * pins here and the clock in board.cmake, and selecting it with -DBOARD=<board>:
 * no file outside boards/ has to change.
 *
 * Pins are spelled the way ch32fun addresses them, port * 16 + number
 * (ch32l103hw.h: PA5, PB10, ...); board.c turns that into the CFGLR/BSHR writes.
 *
 *   USB         : D+ = PA11 (UDP), D- = PA12 (UDM) - dedicated USBFS pins
 *   RGB LED     : PB8 (red), PB6 (blue), PB7 (green) through series resistors to
 *                 VDD_3V3, so a low pin lights that colour
 *   USART1      : PA9 (TX) / PA10 (RX), on the pin header
 *   SWD/JTAG    : PA13 (SWDIO) / PA14 (SWCLK), PA4..PA7 = TDO/TDI/TCK/DIO
 *
 * There is no user button and no CAN transceiver on this board.
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define BOARD_NAME              "ch32l103_ob"

/* ---- user button ----
 * None: DFU is entered through the application detach path (dfu-util -e), or by
 * the bootloader's own decision when the application image is missing. */
#define BOARD_HAS_BOOT_BUTTON   0

/* ---- RGB LED: three discrete LEDs, not an addressable part ----
 * Board order is red / blue / green, which the board layer exposes as
 *      LED  = red   (PB8)
 *      LED1 = blue  (PB6)
 *      LED2 = green (PB7)
 * Each has its own series resistor to VDD_3V3, so a low pin lights it.  Mixing
 * them gives the usual colours (red+green = yellow, all three = white). */
#define BOARD_HAS_LED           1
#define BOARD_LED_PIN           PB8         /* red   */
#define BOARD_LED_ACTIVE_LOW    1
#define BOARD_HAS_LED1          1
#define BOARD_LED1_PIN          PB6         /* blue  */
#define BOARD_HAS_LED2          1
#define BOARD_LED2_PIN          PB7         /* green */

/* ---- application UART: USART1 on the header ----
 * board_init_app_uart() sets up the instance and the two pins. */
#define BOARD_HAS_APP_UART        1
#define BOARD_APP_UART            USART1
#define BOARD_APP_UART_RCC        RCC_APB2Periph_USART1
#define BOARD_APP_UART_BAUDRATE   115200
#define BOARD_APP_UART_TX_PIN     PA9
#define BOARD_APP_UART_RX_PIN     PA10

/* ---- transceiver termination: none on this board ---- */
#define BOARD_HAS_TERMINATION     0

/* ---- GPIO port clocks this board needs ----
 * RCC->APB2PCENR bits, one per port named above: B for the RGB LED, A for the
 * USART1 pins. */
#define BOARD_GPIO_RCC  (RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB)

/* ---- periodic tick ----
 * BOARD_TICK_TIM_ISR has to be the handler name ch32fun's vector table uses
 * for that timer, otherwise the tick interrupt never arrives (TIM3_IRQHandler,
 * TIM2_IRQHandler, ... are all in it). */
#define BOARD_TICK_TIM        TIM3
#define BOARD_TICK_TIM_RCC    RCC_APB1Periph_TIM3
#define BOARD_TICK_TIM_IRQ    TIM3_IRQn
#define BOARD_TICK_TIM_ISR    TIM3_IRQHandler

#endif /* BOARD_CONFIG_H */
