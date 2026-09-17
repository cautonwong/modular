#ifndef SOC_MPS2_H
#define SOC_MPS2_H

#include <stdint.h>

/*
 * SoC support package: ARM MPS2 AN386 (Cortex-M4).
 *
 * Vendor/SoC memory map and IRQ numbers live here so a board selects an SoC
 * instead of hard-coding addresses (ADR D48/D49). Addresses are uintptr_t so the
 * header also compiles on a 64-bit host for analysis.
 */
#define SOC_MPS2_TIMER0_BASE ((uintptr_t)0x40000000u)
#define SOC_MPS2_TIMER_CTRL 0x00u
#define SOC_MPS2_TIMER_RELOAD 0x08u
#define SOC_MPS2_TIMER_INTSTATUS 0x0cu
#define SOC_MPS2_TIMER_CTRL_ENABLE (1u << 0)
#define SOC_MPS2_TIMER_CTRL_IRQEN (1u << 3)
#define SOC_MPS2_TIMER0_IRQ 8u

#define SOC_MPS2_NVIC_ISER0 (*(volatile uint32_t *)(uintptr_t)0xe000e100u)

#endif
