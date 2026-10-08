/*
 * The smallest thing that runs on ChibiOS here: bring the hardware and the kernel up, create one
 * thread, and let it tick. It exists to be a path rather than a feature - the phase's contract asks
 * for a firmware target that builds and something that runs on it, and this is the running part.
 *
 * Nothing of this repository's modules is used yet, and deliberately: what is being shown is that
 * the vendored kernel and HAL start and schedule, which is the foundation a board product would sit
 * on. It is not linked into the default build, for the same reason the kernel target is not: until
 * a product runs ChibiOS, nothing else should depend on this being right.
 */
#include "ch.h"
#include "hal.h"

static THD_WORKING_AREA(wa_heartbeat, 256);

static THD_FUNCTION(heartbeat, arg) {
    (void)arg;
    chRegSetThreadName("heartbeat");

    for (;;) {
        chThdSleepMilliseconds(100);
    }
}

int main(void) {
    halInit();
    chSysInit();

    (void)chThdCreateStatic(wa_heartbeat, sizeof(wa_heartbeat), NORMALPRIO, heartbeat, NULL);

    /* The idle thread is what runs from here, as ChibiOS intends: main becomes idle on return. */
    chThdExit(MSG_OK);
    return 0;
}
