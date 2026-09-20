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

/*
 * NVIC interrupt priorities. The MPS2 Cortex-M4 implements 4 priority bits, so a
 * library priority (0 = highest .. 15) is encoded in the upper 4 bits of the
 * byte at 0xE000E400 + irq - the same encoding BASEPRI uses, which is why an IRQ
 * that calls an RTOS *FromISR API must be set at or numerically below
 * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY. The reset value is 0 (highest),
 * i.e. an RTOS ISR API called from a default-priority IRQ is a priority
 * violation the kernel asserts on.
 */
#define SOC_MPS2_NVIC_IPR ((volatile uint8_t *)(uintptr_t)0xe000e400u)
#define SOC_MPS2_NVIC_PRIO_BITS 4u

static inline void soc_mps2_nvic_set_priority(uint32_t irq, uint8_t library_priority) {
    SOC_MPS2_NVIC_IPR[irq] =
        (uint8_t)((uint32_t)library_priority << (8u - SOC_MPS2_NVIC_PRIO_BITS));
}

#endif
