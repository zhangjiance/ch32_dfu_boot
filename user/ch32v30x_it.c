/********************************** (C) COPYRIGHT *******************************
* File Name          : ch32v30x_it.c
* Description        : Main Interrupt Service Routines for the DFU bootloader.
*
* The USBHS vector is handled in the chip port
* (port/ch32v30x/usb_dc_ch32v30x.c), which forwards it to USBD_IRQHandler().
*******************************************************************************/
#include "ch32v30x_it.h"

void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

/*********************************************************************
 * @fn      NMI_Handler
 *********************************************************************/
void NMI_Handler(void)
{
    while (1) {
    }
}

/*********************************************************************
 * @fn      HardFault_Handler
 *********************************************************************/
void HardFault_Handler(void)
{
    NVIC_SystemReset();
    while (1) {
    }
}
