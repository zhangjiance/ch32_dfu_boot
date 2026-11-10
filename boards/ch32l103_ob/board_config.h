/*
 * board_config.h - "ch32l103_ob"
 *
 * Board Support Package for the "OB" target board populated with a
 * CH32L103C8T6: same footprint and pin map as boards/ch32v30x_ob (LED PA5, boot
 * button PA6, USART3 on PB10/PB11, TIM3 tick), but the part runs from its
 * internal oscillator and enumerates on the full-speed USBFS cell.
 *
 * Only the clock tree and the USB controller differ; both are expressed here
 * and in board.cmake, so the shared board.c and board.h are used unchanged.
 *
 * Each board lives in its own, self-contained folder under boards/:
 *     boards/<board>/board_config.h   pins / polarity / features (this file)
 *     boards/<board>/board.h          board primitive interface (copy as is)
 *     boards/<board>/board.c          board primitive implementation (copy as is)
 *     boards/<board>/board.cmake      crystal + clock tree, optional extras
 *
 * Adding a board should be nothing more than copying this folder, editing this
 * file and the crystal in board.cmake, and selecting it with -DBOARD=<board>:
 * no file outside boards/ has to change.
 *
 * Pins are spelled the way ch32fun addresses them, port * 16 + number
 * (ch32v30xhw.h: PA5, PB10, ...); board.c turns that into the CFGLR/BSHR
 * writes.
 *
 *   USB         : D+ = PA11 (UDP), D- = PA12 (UDM) - dedicated USBFS pins
 *   RGB LED     : PB14 (red), PB13 (blue), PB15 (green), each through 2k2 to
 *                 VDD_3V3, so all three are active low
 *   user button : PC13 to GND (switch SW-PB4P), active low
 *   UART1       : PA9 (TX) / PA10 (RX), broken out on the pin header
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define BOARD_NAME              "ch32l103_ob"

/* ---- user button: PC13 to GND, pull-up, active low ----
 * Set BOARD_HAS_BOOT_BUTTON to 0 on a board without a button: DFU is then
 * entered only through the application detach path (dfu-util -e). */
#define BOARD_HAS_BOOT_BUTTON   1
#define BOARD_BOOT_PIN          PC13
#define BOARD_BOOT_ACTIVE_LOW   1

/* ---- RGB LED: three discrete LEDs, not an addressable part ----
 * PB14 = red, PB13 = blue, PB15 = green, each with its own series resistor to
 * VDD_3V3, so a low pin lights that colour.  Applications drive them through
 * board_led_write() / board_led2_write() / board_led3_write(), in this order:
 *      LED1 = red, LED2 = blue, LED3 = green
 * Mixing them gives the usual colours (red+green = yellow, all three = white). */
#define BOARD_HAS_LED           1
#define BOARD_LED_PIN           PB14        /* red   */
#define BOARD_LED_ACTIVE_LOW    1
#define BOARD_HAS_LED2          1
#define BOARD_LED2_PIN          PB13        /* blue  */
#define BOARD_HAS_LED3          1
#define BOARD_LED3_PIN          PB15        /* green */

/* ---- application UART ----
 * The board's UART for application traffic; board_init_app_uart() sets up the
 * instance and the two pins.  The images shipped in this project never call
 * it, so it only has to be right for boards that do. */
#define BOARD_HAS_APP_UART        1
#define BOARD_APP_UART            USART1
#define BOARD_APP_UART_RCC        RCC_APB2Periph_USART1
#define BOARD_APP_UART_BAUDRATE   115200
#define BOARD_APP_UART_TX_PIN     PA9
#define BOARD_APP_UART_RX_PIN     PA10

/* ---- USB kernel clock ----
 * The USBFS cell wants 48 MHz.  With this board's 72 MHz core (board.cmake)
 * that is PLLCLK / 1.5, where a 96 MHz crystal board would use /2.  Expressed
 * here because it follows from the clock tree, which is a board property. */
#define BOARD_USB_PRESCALER     RCC_USBCLKSource_PLLCLK_Div1_5

/* ---- GPIO port clocks this board needs ----
 * RCC->APB2PCENR bits, one per port named above. */
#define BOARD_GPIO_RCC  (RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC)

/* ---- periodic tick ----
 * BOARD_TICK_TIM_ISR has to be the handler name ch32fun's vector table uses
 * for that timer, otherwise the tick interrupt never arrives (TIM3_IRQHandler,
 * TIM2_IRQHandler, ... are all in it). */
#define BOARD_TICK_TIM        TIM3
#define BOARD_TICK_TIM_RCC    RCC_APB1Periph_TIM3
#define BOARD_TICK_TIM_IRQ    TIM3_IRQn
#define BOARD_TICK_TIM_ISR    TIM3_IRQHandler

#endif /* BOARD_CONFIG_H */
