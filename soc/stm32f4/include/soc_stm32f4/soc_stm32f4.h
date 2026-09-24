#ifndef SOC_STM32F4_H
#define SOC_STM32F4_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Memory Map Base Addresses */
#define SOC_STM32F4_FLASH_BASE ((uintptr_t)0x08000000u)
#define SOC_STM32F4_SRAM_BASE ((uintptr_t)0x20000000u)
#define SOC_STM32F4_CCM_BASE ((uintptr_t)0x10000000u)

#define SOC_STM32F4_TIM1_BASE ((uintptr_t)0x40010000u)
#define SOC_STM32F4_TIM8_BASE ((uintptr_t)0x40010400u)
#define SOC_STM32F4_ADC1_BASE ((uintptr_t)0x40012000u)
#define SOC_STM32F4_ADC2_BASE ((uintptr_t)0x40012100u)
#define SOC_STM32F4_ADC3_BASE ((uintptr_t)0x40012200u)
#define SOC_STM32F4_SPI1_BASE ((uintptr_t)0x40013000u)
#define SOC_STM32F4_SPI2_BASE ((uintptr_t)0x40003800u)
#define SOC_STM32F4_SPI3_BASE ((uintptr_t)0x40003C00u)
#define SOC_STM32F4_CAN1_BASE ((uintptr_t)0x40006400u)
#define SOC_STM32F4_CAN2_BASE ((uintptr_t)0x40006800u)
#define SOC_STM32F4_RCC_BASE ((uintptr_t)0x40023800u)

/* Clock Frequencies for STM32F405/F407 */
#define SOC_STM32F4_SYSCLK_HZ 168000000u
#define SOC_STM32F4_TIM_CLK_HZ 168000000u
#define SOC_STM32F4_APB1_CLK_HZ 42000000u
#define SOC_STM32F4_APB2_CLK_HZ 84000000u

/* IRQ Numbers */
#define SOC_STM32F4_TIM1_UP_TIM10_IRQn 25
#define SOC_STM32F4_TIM1_CC_IRQn 27
#define SOC_STM32F4_ADC_IRQn 18
#define SOC_STM32F4_CAN1_TX_IRQn 19
#define SOC_STM32F4_CAN1_RX0_IRQn 20

/* Calculations & Register Helpers */
uint32_t soc_stm32f4_calc_pwm_arr(uint32_t timer_clk_hz, uint32_t switching_freq_hz);
uint8_t soc_stm32f4_calc_deadtime_reg(float deadtime_ns, uint32_t timer_clk_hz);

#ifdef __cplusplus
}
#endif

#endif /* SOC_STM32F4_H */
