#include "app_select/app_select.h"

/*
 * applications/app.c:101-…, its own switch turned into a table. Each configured application is the
 * parts the reference starts for it - up to two of them - and the two arguments it passes: the ADC
 * and the pedal sensor are each told whether the serial port is theirs, which is what makes one of
 * them the comm port's partner and the other the pedal sensor's. A configuration the table does not
 * name starts nothing, which is what the reference's own switch falls through to.
 *
 * Two of the cases were read from the enumeration's own names rather than from the switch's body -
 * the Nordic radio's and a custom one's - and are carried as their own part: what the reference
 * starts for them is a product's, as it is for every other part here.
 */
app_plan_t app_plan_for(uint8_t app_to_use, bool servo_out_enable) {
    app_plan_t plan = {
        .parts = 0u, .adc_own_uart = false, .pas_own_uart = false, .servo_decoder = false};

    /* :91-100: the servo decoder runs in place of the PPM input, and only then. */
    plan.servo_decoder =
        (app_to_use != APP_USE_PPM) && (app_to_use != APP_USE_PPM_UART) && servo_out_enable;

    switch (app_to_use) {
    case APP_USE_PPM:
        plan.parts = APP_PART_PPM;
        break;

    case APP_USE_ADC:
        plan.parts = APP_PART_ADC;
        plan.adc_own_uart = true; /* app_adc_start(true) */
        break;

    case APP_USE_UART:
        plan.parts = APP_PART_UART_COMM;
        break;

    case APP_USE_PPM_UART:
        plan.parts = APP_PART_PPM | APP_PART_UART_COMM;
        break;

    case APP_USE_ADC_UART:
        plan.parts = APP_PART_ADC | APP_PART_UART_COMM;
        break;

    case APP_USE_NUNCHUK:
        plan.parts = APP_PART_NUNCHUK;
        break;

    case APP_USE_NRF:
        plan.parts = APP_PART_NRF;
        break;

    case APP_USE_CUSTOM:
        plan.parts = APP_PART_CUSTOM;
        break;

    case APP_USE_PAS:
        plan.parts = APP_PART_PAS;
        plan.pas_own_uart = true; /* app_pas_start(true) */
        break;

    case APP_USE_ADC_PAS:
        plan.parts = APP_PART_ADC | APP_PART_PAS;
        break;

    default:
        break;
    }

    return plan;
}
