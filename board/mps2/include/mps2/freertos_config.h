#ifndef BOARD_MPS2_FREERTOS_CONFIG_H
#define BOARD_MPS2_FREERTOS_CONFIG_H

/*
 * Board-scoped FreeRTOS configuration (D87): the MPS2 AN386 image clocks its
 * CMSDK timer at 25 MHz. A different board changes this file, not the PAL.
 */
#define configCPU_CLOCK_HZ (25000000UL)
#define configTICK_RATE_HZ ((TickType_t)1000)

#endif
