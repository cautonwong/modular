#ifndef APP_APP_SELECT_H
#define APP_APP_SELECT_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * applications/app.c:101-…, the table the reference's dispatcher switches on: a configured
 * application is not one thing but up to two - a PPM one with the serial comm port, an ADC one with
 * either comm or a pedal sensor, and so on - and the servo decoder that runs instead of the PPM
 * input is its own decision. Starting the parts is the product's; which parts and which way they
 * are told to start is this.
 */
typedef enum app_part {
    APP_PART_PPM = 1u << 0,
    APP_PART_ADC = 1u << 1,
    APP_PART_UART_COMM = 1u << 2,
    APP_PART_NUNCHUK = 1u << 3,
    APP_PART_PAS = 1u << 4,
    APP_PART_NRF = 1u << 5,
    APP_PART_CUSTOM = 1u << 6
} app_part_t;

/* app_use (datatypes.h:601-612), the configuration's own choice of application. */
#define APP_USE_NONE 0u
#define APP_USE_PPM 1u
#define APP_USE_ADC 2u
#define APP_USE_UART 3u
#define APP_USE_PPM_UART 4u
#define APP_USE_ADC_UART 5u
#define APP_USE_NUNCHUK 6u
#define APP_USE_NRF 7u
#define APP_USE_CUSTOM 8u
#define APP_USE_PAS 9u
#define APP_USE_ADC_PAS 10u

/*
 * What a configured application is made of, in the reference's own table: the parts to start, the
 * two arguments two of them are told - the ADC and the pedal sensor each have a way of being told
 * that the serial port is not theirs - and whether the servo decoder is the one that runs.
 */
typedef struct app_plan {
    uint32_t parts;
    bool adc_own_uart;
    bool pas_own_uart;
    bool servo_decoder;
} app_plan_t;

/*
 * :101-…, the decision itself: the table above, plus the servo decoder's own rule (:91-100), which
 * is that it runs when the configured application is neither of the two PPM ones and the servo
 * output is enabled.
 */
app_plan_t app_plan_for(uint8_t app_to_use, bool servo_out_enable);

#ifdef __cplusplus
}
#endif

#endif /* APP_APP_SELECT_H */
