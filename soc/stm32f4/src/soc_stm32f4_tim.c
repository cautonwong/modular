#include "soc_stm32f4/soc_stm32f4.h"

/*
 * The register fields, from RM0090's advanced-control timer chapter: CR1 in 18.4.1 (CEN, UDIS,
 * CMS, ARPE), EGR in 18.4.5 (UG), CCMR1/2 in 18.4.7 (OCxM, OCxPE), CCER in 18.4.9 (CCxE, CCxNE),
 * BDTR in 18.4.13 (DTG, OSSI, OSSR, MOE).
 *
 * The names are prefixed rather than taken from CMSIS: this file is the one that is supposed to
 * spell the fields out, and a second set of names for the same bits invites the two to disagree.
 */
#define SOC_TIM_CR1_CEN (1u << 0)
#define SOC_TIM_CR1_UDIS (1u << 1)
#define SOC_TIM_CR1_CMS_CENTER1 (1u << 5)
#define SOC_TIM_CR1_ARPE (1u << 7)

#define SOC_TIM_EGR_UG (1u << 0)

/* PWM mode 1 (OCxM = 110) with the compare preload on, on all four compare channels. ChibiOS's
 * TIMv1 PWM LLD writes the same encoding for every channel it drives (pwm_lld.c: OCxM(6) | OCxPE),
 * which is why it is one expression per half-register rather than a computed shift. */
#define SOC_TIM_CCMR_OCM_PWM1 (6u << 4)
#define SOC_TIM_CCMR_OCPE (1u << 3)
#define SOC_TIM_CCMR_OC2M_PWM1 (6u << 12)
#define SOC_TIM_CCMR_OC2PE (1u << 11)

#define SOC_TIM_CCER_CC1E (1u << 0)
#define SOC_TIM_CCER_CC1NE (1u << 2)
#define SOC_TIM_CCER_CC2E (1u << 4)
#define SOC_TIM_CCER_CC2NE (1u << 6)
#define SOC_TIM_CCER_CC3E (1u << 8)
#define SOC_TIM_CCER_CC3NE (1u << 10)

#define SOC_TIM_BDTR_DTG_MASK 0xFFu
#define SOC_TIM_BDTR_OSSI (1u << 10)
#define SOC_TIM_BDTR_OSSR (1u << 11)
#define SOC_TIM_BDTR_MOE (1u << 15)

uint32_t soc_stm32f4_tim_init(soc_stm32f4_tim_regs_t *tim, const soc_stm32f4_tim_config_t *config) {
    if (tim == NULL || config == NULL || config->timer_clk_hz == 0u ||
        config->switching_freq_hz == 0u) {
        return 0u;
    }

    const uint32_t arr = soc_stm32f4_calc_pwm_arr(config->timer_clk_hz, config->switching_freq_hz);
    if (arr == 0u) {
        /* The timer cannot divide that far down: a switching frequency the clock cannot resolve
         * would leave ARR at its reset value, i.e. a period of one count. */
        return 0u;
    }
    const uint8_t dtg = soc_stm32f4_calc_deadtime_reg(config->deadtime_ns, config->timer_clk_hz);

    /* Stopped while it is being configured. */
    tim->cr1 = 0u;
    /* No prescaler: the period carries the whole division, so the compare values stay in the
     * timer's own counts and the resolution of the duty is the period's. */
    tim->psc = 0u;
    tim->arr = arr;
    tim->rcr = 0u;

    tim->ccmr1 =
        SOC_TIM_CCMR_OCM_PWM1 | SOC_TIM_CCMR_OCPE | SOC_TIM_CCMR_OC2M_PWM1 | SOC_TIM_CCMR_OC2PE;
    tim->ccmr2 =
        SOC_TIM_CCMR_OCM_PWM1 | SOC_TIM_CCMR_OCPE | SOC_TIM_CCMR_OC2M_PWM1 | SOC_TIM_CCMR_OC2PE;
    /* The three phases and their three complements, so a low-side gate is driven by the timer
     * rather than by its own output. */
    tim->ccer = SOC_TIM_CCER_CC1E | SOC_TIM_CCER_CC1NE | SOC_TIM_CCER_CC2E | SOC_TIM_CCER_CC2NE |
                SOC_TIM_CCER_CC3E | SOC_TIM_CCER_CC3NE;
    /* Dead time, both off-state selections on so the outputs sit in their idle state while MOE is
     * clear, and MOE itself left clear: the outputs are not energised until asked, which is the
     * only part of this that can put voltage on a motor. */
    tim->bdtr = ((uint32_t)dtg & SOC_TIM_BDTR_DTG_MASK) | SOC_TIM_BDTR_OSSI | SOC_TIM_BDTR_OSSR;
    /* One update event to load PSC, ARR and the preloaded compares before the counter runs. */
    tim->egr = SOC_TIM_EGR_UG;
    tim->cr1 = SOC_TIM_CR1_CMS_CENTER1 | SOC_TIM_CR1_ARPE | SOC_TIM_CR1_CEN;
    return arr;
}

bool soc_stm32f4_tim_set_duty_counts(soc_stm32f4_tim_regs_t *tim, uint32_t ccr1, uint32_t ccr2,
                                     uint32_t ccr3, bool swap_phase_order) {
    if (tim == NULL || ccr1 > tim->arr || ccr2 > tim->arr || ccr3 > tim->arr) {
        return false;
    }

    /*
     * The reference's duty macros (motor/mcpwm_foc.c:80-104): the update interrupt is disabled
     * around the three writes so the compares change together rather than across an update, and
     * re-enabled afterwards. On one of the two timers the phases arrive in the order 1, 3, 2.
     */
    tim->cr1 |= SOC_TIM_CR1_UDIS;
    tim->ccr1 = ccr1;
    tim->ccr2 = swap_phase_order ? ccr3 : ccr2;
    tim->ccr3 = swap_phase_order ? ccr2 : ccr3;
    tim->cr1 &= ~SOC_TIM_CR1_UDIS;
    return true;
}

void soc_stm32f4_tim_enable_outputs(soc_stm32f4_tim_regs_t *tim) {
    if (tim == NULL) {
        return;
    }
    tim->bdtr |= SOC_TIM_BDTR_MOE;
}

void soc_stm32f4_tim_disable_outputs(soc_stm32f4_tim_regs_t *tim) {
    if (tim == NULL) {
        return;
    }
    tim->bdtr &= ~SOC_TIM_BDTR_MOE;
}
