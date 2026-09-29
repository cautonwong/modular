#include "soc_stm32f4/soc_stm32f4.h"

/*
 * The ADC fields, from RM0090's chapter 13.13. The trigger selection is the one the reference's
 * own configuration asks for: ADC_ExternalTrigInjecConv_T1_TRGO is 0x00010000 in the library it
 * uses (stm32f4xx_adc.h:408), i.e. JEXTSEL = 1, and the enable of that trigger is JEXTEN = 01,
 * a rising edge (RM0090 13.13.3), which is what makes a timer edge start the conversion.
 *
 * The interrupt bit falls out of the same library's ADC_ITConfig, which shifts one left by the
 * low byte of the identifier: ADC_IT_JEOC is 0x0407, so JEOCIE is bit 7 of CR1 and the flag it
 * pairs with is bit 2 of SR (ADC_FLAG_JEOC, 0x04).
 */
#define SOC_ADC_SR_EOC (1u << 1)
#define SOC_ADC_SR_JEOC (1u << 2)

#define SOC_ADC_CR1_ADON (1u << 0)
#define SOC_ADC_CR1_JEOCIE (1u << 7)

/* Software start of a regular conversion is bit 30 of CR2, and the two bits below it are the
 * external trigger's edge selection, which this leaves disabled. */
#define SOC_ADC_CR2_SWSTART (1u << 30)
#define SOC_ADC_CR2_EXTEN_MASK (3u << 28)
#define SOC_ADC_CR2_JEXTEN_RISING (1u << 20)
#define SOC_ADC_CR2_JEXTSEL_TIM1_TRGO (1u << 16)

#define SOC_ADC_JSQR_JL_SHIFT 20u
#define SOC_ADC_JSQR_JSQ1_SHIFT 15u
#define SOC_ADC_JSQR_JSQ2_SHIFT 10u
#define SOC_ADC_JSQR_JSQ3_SHIFT 5u
#define SOC_ADC_JSQR_JSQ4_SHIFT 0u
#define SOC_ADC_JSQR_JSQ_MASK 0x1Fu

/* The regular sequence: its length in SQR1's L field (23:20, one less than the count) and each
 * rank's channel in SQR3's five-bit fields, the first rank at bit 0 (RM0090 13.13.6). */
#define SOC_ADC_SQR1_L_SHIFT 20u
#define SOC_ADC_SQR3_SQ1_SHIFT 0u
#define SOC_ADC_SQR_SQ_MASK 0x1Fu

/* One injected conversion per rank, all of them on the same channel: the reference's own sequence
 * (hw_60_core.c:205-213) is that channel at ranks 1, 2 and 3. */
static uint32_t adc_jsqr_for(uint8_t channel, uint8_t samples) {
    const uint32_t jsq = (uint32_t)channel & SOC_ADC_JSQR_JSQ_MASK;
    const uint32_t jl = (uint32_t)(samples - 1u) << SOC_ADC_JSQR_JL_SHIFT;
    switch (samples) {
    case 1u:
        return jl | (jsq << SOC_ADC_JSQR_JSQ1_SHIFT);
    case 2u:
        return jl | (jsq << SOC_ADC_JSQR_JSQ1_SHIFT) | (jsq << SOC_ADC_JSQR_JSQ2_SHIFT);
    case 3u:
        return jl | (jsq << SOC_ADC_JSQR_JSQ1_SHIFT) | (jsq << SOC_ADC_JSQR_JSQ2_SHIFT) |
               (jsq << SOC_ADC_JSQR_JSQ3_SHIFT);
    default:
        return jl | (jsq << SOC_ADC_JSQR_JSQ1_SHIFT) | (jsq << SOC_ADC_JSQR_JSQ2_SHIFT) |
               (jsq << SOC_ADC_JSQR_JSQ3_SHIFT) | (jsq << SOC_ADC_JSQR_JSQ4_SHIFT);
    }
}

/* The sample time lives in SMPR1 for channels 10 to 17 and SMPR2 for 0 to 9, three bits each. */
static void adc_set_sample_time(soc_stm32f4_adc_regs_t *adc, uint8_t channel, uint8_t sample_time) {
    const uint32_t field = 3u * (uint32_t)(channel < 10u ? channel : (uint8_t)(channel - 10u));
    if (channel < 10u) {
        adc->smpr2 = (adc->smpr2 & ~(0x7u << field)) | ((uint32_t)sample_time << field);
    } else {
        adc->smpr1 = (adc->smpr1 & ~(0x7u << field)) | ((uint32_t)sample_time << field);
    }
}

uint32_t soc_stm32f4_adc_init_injected(soc_stm32f4_adc_regs_t *adc,
                                       const soc_stm32f4_adc_config_t *config) {
    if (adc == NULL || config == NULL || config->samples == 0u || config->samples > 4u ||
        config->sample_time > 7u || config->channel > 17u) {
        return 0u;
    }

    /* Enabled while it is configured, as the manual requires for the settings to take effect. */
    adc->cr1 = SOC_ADC_CR1_ADON | (config->jeoc_interrupt ? SOC_ADC_CR1_JEOCIE : 0u);
    adc->cr2 = SOC_ADC_CR2_JEXTSEL_TIM1_TRGO | SOC_ADC_CR2_JEXTEN_RISING;
    adc_set_sample_time(adc, config->channel, config->sample_time);
    adc->jsqr = adc_jsqr_for(config->channel, config->samples);
    return (uint32_t)config->samples;
}

bool soc_stm32f4_adc_read_injected(const soc_stm32f4_adc_regs_t *adc, uint8_t samples,
                                   uint16_t *out) {
    if (adc == NULL || out == NULL || samples == 0u || samples > 4u) {
        return false;
    }

    /* JDRx holds the result of the rank of the same number, in the order the sequence ran. */
    volatile const uint32_t *jdr = &adc->jdr1;
    for (uint8_t i = 0u; i < samples; ++i) {
        out[i] = (uint16_t)(jdr[i] & 0xFFFFu);
    }
    return true;
}

static void (*injected_handler)(void *ctx, uint32_t adc_index);
static void *injected_handler_ctx;

void soc_stm32f4_adc_set_injected_handler(void (*handler)(void *ctx, uint32_t adc_index),
                                          void *ctx) {
    injected_handler = handler;
    injected_handler_ctx = ctx;
}

uint32_t soc_stm32f4_adc_init_regular(soc_stm32f4_adc_regs_t *adc, uint8_t channel,
                                      uint8_t sample_time) {
    if (adc == NULL || channel > 17u || sample_time > 7u) {
        return 0u;
    }

    adc_set_sample_time(adc, channel, sample_time);
    /* A sequence of one: the length field is the count minus one (RM0090 13.13.6), and the
     * channel goes in the first rank of SQR3. The external trigger for the regular group stays
     * disabled, because the read below starts the conversion itself. */
    adc->sqr1 = 0u << SOC_ADC_SQR1_L_SHIFT;
    adc->sqr3 = ((uint32_t)channel & SOC_ADC_SQR_SQ_MASK) << SOC_ADC_SQR3_SQ1_SHIFT;
    adc->cr2 &= ~SOC_ADC_CR2_EXTEN_MASK;
    return 1u;
}

bool soc_stm32f4_adc_read_regular(soc_stm32f4_adc_regs_t *adc, uint16_t *out) {
    if (adc == NULL || out == NULL || (adc->cr1 & SOC_ADC_CR1_ADON) == 0u) {
        return false;
    }

    adc->cr2 |= SOC_ADC_CR2_SWSTART;
    /* Bounded, so a part that never converts is reported rather than waited on forever. The
     * count is a guard rather than a timing constant: a conversion is a few hundred nanoseconds. */
    for (uint32_t spin = 0u; spin < 100000u; ++spin) {
        if ((adc->sr & SOC_ADC_SR_EOC) != 0u) {
            /* EOC is cleared by writing zero to it (RM0090 13.13.1); the ones elsewhere in this
             * write are no-ops on the part. */
            adc->sr = ~SOC_ADC_SR_EOC;
            *out = (uint16_t)(adc->dr & 0xFFFFu);
            return true;
        }
    }
    return false;
}

void soc_stm32f4_adc_service_injected_isr(soc_stm32f4_adc_regs_t *adc, uint32_t adc_index) {
    if (adc == NULL || (adc->sr & SOC_ADC_SR_JEOC) == 0u) {
        return;
    }

    /* Cleared before the work is handed on: a second conversion finishing during the handler would
     * otherwise be lost by the write below. */
    adc->sr = ~SOC_ADC_SR_JEOC;
    if (injected_handler != NULL) {
        injected_handler(injected_handler_ctx, adc_index);
    }
}
