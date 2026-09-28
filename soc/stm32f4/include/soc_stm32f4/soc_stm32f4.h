#ifndef SOC_STM32F4_H
#define SOC_STM32F4_H

#include <stdbool.h>
#include <stddef.h>
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

/*
 * TIM1/TIM8 as the reference manual lays them out - RM0090, the register map at the head of the
 * advanced-control timer chapter, with CR1 in 18.4.1, EGR in 18.4.5, CCMR1/2 in 18.4.7, CCER in
 * 18.4.9, ARR in 18.4.4, CCRx in 18.4.10 and BDTR in 18.4.13. The struct *is* the register map, so
 * the offsets are asserted below rather than assumed: a field in the wrong place would compile and
 * then write the wrong register, which is a failure no host test could see and no board would
 * survive quietly.
 *
 * The base addresses are the ones at the top of this header. A host test binds this same layout
 * to a block of memory, which is how the settings are checked without the part.
 */
typedef struct soc_stm32f4_tim_regs {
    volatile uint32_t cr1;   /* 0x00 */
    volatile uint32_t cr2;   /* 0x04 */
    volatile uint32_t smcr;  /* 0x08 */
    volatile uint32_t dier;  /* 0x0C */
    volatile uint32_t sr;    /* 0x10 */
    volatile uint32_t egr;   /* 0x14 */
    volatile uint32_t ccmr1; /* 0x18 */
    volatile uint32_t ccmr2; /* 0x1C */
    volatile uint32_t ccer;  /* 0x20 */
    volatile uint32_t cnt;   /* 0x24 */
    volatile uint32_t psc;   /* 0x28 */
    volatile uint32_t arr;   /* 0x2C */
    volatile uint32_t rcr;   /* 0x30 */
    volatile uint32_t ccr1;  /* 0x34 */
    volatile uint32_t ccr2;  /* 0x38 */
    volatile uint32_t ccr3;  /* 0x3C */
    volatile uint32_t ccr4;  /* 0x40 */
    volatile uint32_t bdtr;  /* 0x44 */
} soc_stm32f4_tim_regs_t;

_Static_assert(offsetof(soc_stm32f4_tim_regs_t, egr) == 0x14u, "EGR is at 0x14 (RM0090 18.4.5)");
_Static_assert(offsetof(soc_stm32f4_tim_regs_t, ccmr1) == 0x18u,
               "CCMR1 is at 0x18 (RM0090 18.4.7)");
_Static_assert(offsetof(soc_stm32f4_tim_regs_t, ccer) == 0x20u, "CCER is at 0x20 (RM0090 18.4.9)");
_Static_assert(offsetof(soc_stm32f4_tim_regs_t, arr) == 0x2Cu, "ARR is at 0x2C (RM0090 18.4.4)");
_Static_assert(offsetof(soc_stm32f4_tim_regs_t, ccr1) == 0x34u, "CCR1 is at 0x34 (RM0090 18.4.10)");
_Static_assert(offsetof(soc_stm32f4_tim_regs_t, bdtr) == 0x44u, "BDTR is at 0x44 (RM0090 18.4.13)");
_Static_assert(sizeof(soc_stm32f4_tim_regs_t) == 0x48u, "the block ends after BDTR");

/* What the control loop asks of the timer: the clock it runs at, the switching frequency the
 * configuration is set to, and the dead time the board's gate driver needs. */
typedef struct soc_stm32f4_tim_config {
    uint32_t timer_clk_hz;
    uint32_t switching_freq_hz;
    float deadtime_ns;
} soc_stm32f4_tim_config_t;

/* Configured at the frequency asked for, center-aligned, with the counter running and the
 * outputs still off: enabling them is a separate call because it is the one that can put voltage
 * on a motor. Returns the period it programmed, or 0 when the configuration is refused - the same
 * "zero means it could not be done" the calculation helpers above already use, and the reason
 * this layer depends on nothing: it answers in its own terms. */
uint32_t soc_stm32f4_tim_init(soc_stm32f4_tim_regs_t *tim, const soc_stm32f4_tim_config_t *config);
/* Compare values in timer counts, as the reference's own duty macros take them
 * (motor/mcpwm_foc.c:80-104), written with the update interrupt disabled around all three.
 * False when a compare is past the period, in which case nothing was written. */
bool soc_stm32f4_tim_set_duty_counts(soc_stm32f4_tim_regs_t *tim, uint32_t ccr1, uint32_t ccr2,
                                     uint32_t ccr3, bool swap_phase_order);
/* MOE, the main output enable: the only bit between a configured timer and a driven motor. */
void soc_stm32f4_tim_enable_outputs(soc_stm32f4_tim_regs_t *tim);
void soc_stm32f4_tim_disable_outputs(soc_stm32f4_tim_regs_t *tim);

/*
 * The ADC, same treatment: RM0090's chapter 13.13 for the register map and the fields - SR in
 * 13.13.1 (JEOC is bit 2), CR1 in 13.13.2 (JEOCIE is bit 7, ADON bit 0), CR2 in 13.13.3
 * (JEXTSEL 19:16, JEXTEN 21:20), SMPR1/2 in 13.13.4, JSQR in 13.13.7 (JL 21:20, JSQ1 19:15,
 * JSQ2 14:10, JSQ3 9:5, JSQ4 4:0) and JDRx in 13.13.9.
 *
 * The reference's own setup is the caller here: one injected channel per ADC, sampled three times
 * over (hw_60_core.c:205-213 configures ADC1 channel 10, ADC2 channel 11 and ADC3 channel 12, each
 * at three injected ranks), triggered from the timer, with the conversion times it asks for
 * (ADC_SampleTime_15Cycles, hw_60_core.c:174). It configures no multi-mode, so each ADC is
 * independent and every one of them is triggered on its own.
 */
typedef struct soc_stm32f4_adc_regs {
    volatile uint32_t sr;    /* 0x00 */
    volatile uint32_t cr1;   /* 0x04 */
    volatile uint32_t cr2;   /* 0x08 */
    volatile uint32_t smpr1; /* 0x0C */
    volatile uint32_t smpr2; /* 0x10 */
    volatile uint32_t jofr1; /* 0x14 */
    volatile uint32_t jofr2; /* 0x18 */
    volatile uint32_t jofr3; /* 0x1C */
    volatile uint32_t jofr4; /* 0x20 */
    volatile uint32_t htr;   /* 0x24 */
    volatile uint32_t ltr;   /* 0x28 */
    volatile uint32_t sqr1;  /* 0x2C */
    volatile uint32_t sqr2;  /* 0x30 */
    volatile uint32_t sqr3;  /* 0x34 */
    volatile uint32_t jsqr;  /* 0x38 */
    volatile uint32_t jdr1;  /* 0x3C */
    volatile uint32_t jdr2;  /* 0x40 */
    volatile uint32_t jdr3;  /* 0x44 */
    volatile uint32_t jdr4;  /* 0x48 */
} soc_stm32f4_adc_regs_t;

_Static_assert(offsetof(soc_stm32f4_adc_regs_t, cr1) == 0x04u, "CR1 is at 0x04 (RM0090 13.13.2)");
_Static_assert(offsetof(soc_stm32f4_adc_regs_t, cr2) == 0x08u, "CR2 is at 0x08 (RM0090 13.13.3)");
_Static_assert(offsetof(soc_stm32f4_adc_regs_t, smpr1) == 0x0Cu,
               "SMPR1 is at 0x0C (RM0090 13.13.4)");
_Static_assert(offsetof(soc_stm32f4_adc_regs_t, jsqr) == 0x38u, "JSQR is at 0x38 (RM0090 13.13.7)");
_Static_assert(offsetof(soc_stm32f4_adc_regs_t, jdr1) == 0x3Cu, "JDR1 is at 0x3C (RM0090 13.13.9)");
_Static_assert(sizeof(soc_stm32f4_adc_regs_t) == 0x4Cu, "the block ends after JDR4");

typedef struct soc_stm32f4_adc_config {
    uint8_t channel;     /* the injected channel; the reference uses 10, 11 and 12 */
    uint8_t samples;     /* injected ranks, 1 to 4; the reference fills three */
    uint8_t sample_time; /* the SMPR field, 0 to 7; the reference asks for 15 cycles (1) */
    bool jeoc_interrupt; /* enable the end-of-injected-conversion interrupt */
} soc_stm32f4_adc_config_t;

/* Returns the number of injected ranks programmed, or 0 when the configuration is refused, in the
 * same terms as the timer above. The injected sequence is one channel repeated `samples` times,
 * and the trigger is TIM1_TRGO on the rising edge. */
uint32_t soc_stm32f4_adc_init_injected(soc_stm32f4_adc_regs_t *adc,
                                       const soc_stm32f4_adc_config_t *config);
/* The converted values, in rank order, from JDR1 upwards. */
bool soc_stm32f4_adc_read_injected(const soc_stm32f4_adc_regs_t *adc, uint8_t samples,
                                   uint16_t *out);
/* The end-of-injected-conversion interrupt is one vector for all three ADCs
 * (SOC_STM32F4_ADC_IRQn), so the hook is one function and it is told which ADC it is servicing. */
void soc_stm32f4_adc_set_injected_handler(void (*handler)(void *ctx, uint32_t adc_index),
                                          void *ctx);
/* Clears the flag and forwards to the hook. Called with the ADC's own register block and its
 * index, so the vector table's entry and the three blocks stay the caller's business. */
void soc_stm32f4_adc_service_injected_isr(soc_stm32f4_adc_regs_t *adc, uint32_t adc_index);

#ifdef __cplusplus
}
#endif

#endif /* SOC_STM32F4_H */
