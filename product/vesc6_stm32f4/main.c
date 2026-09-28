#include "bldc/sys.h"
#include "edge/event.h"
#include "edge/modules.h"
#include "glue.h"
#include "motor_config/motor_config.h"
#include "timeout_guard/timeout_guard.h"
#include "vesc6/board.h"

#include <stdalign.h>
#include <string.h>

/*
 * The VESC6 / STM32F405-407 composition root (D1). The modules are assembled here, explicitly,
 * with every byte of their state provided by this function and no dynamic allocation anywhere.
 *
 * What runs today: the board description, the configuration module over the variable store in
 * glue.c, and the timeout guard, stepped by the bldc family's scheduler. What does not yet run:
 * the control path and the protocol, because their ports are ADC, PWM and UART/CAN reads, and
 * soc/stm32f4 is still only arithmetic and address macros - the register-level drivers are D2,
 * and their landing is what puts foc_core and vesc_comm into the app list below.
 */
int main(void) {
    board_vesc6_t board;
    if (board_vesc6_init(&board) != EDGE_OK) {
        return 10;
    }

    vesc6_glue_state_t glue;
    vesc6_glue_init(&glue, &board);

    motor_config_var_port_t vars;
    vesc6_make_var_port(&vars, &glue);

    /* The configuration's memory is the composition root's to provide, not the module's. */
    alignas(
        MOTOR_CONFIG_STORAGE_ALIGN) static unsigned char config_storage[MOTOR_CONFIG_STORAGE_SIZE];
    motor_config_t *config = (motor_config_t *)config_storage;
    motor_config_construct(config, EDGE_MOD_MOTOR_CONFIG, 30u, &vars);
    if (motor_config_init(config) != EDGE_OK) {
        return 11;
    }

    alignas(
        TIMEOUT_GUARD_STORAGE_ALIGN) static unsigned char guard_storage[TIMEOUT_GUARD_STORAGE_SIZE];
    timeout_guard_t *guard = (timeout_guard_t *)guard_storage;
    timeout_guard_construct(guard, EDGE_MOD_TIMEOUT_GUARD, 5u, NULL, 500u, 5.0f, 1000u);
    if (timeout_guard_init(guard) != EDGE_OK) {
        return 12;
    }

    edge_module_t *apps[2];
    apps[0] = motor_config_module(config);
    apps[1] = timeout_guard_module(guard);

    edge_event_t event_storage[16];
    edge_event_queue_t event_queue;
    if (edge_event_queue_init(&event_queue, event_storage, 16u) != EDGE_OK) {
        return 1;
    }

    edge_sys_subscription_t subscriptions[8];
    edge_sys_t sys;
    if (sys_bldc_init(&sys, apps, 2u, &event_queue, subscriptions, 8u) != EDGE_OK) {
        return 2;
    }
    /* Nothing polls until the system is started: edge_sys_step() answers EDGE_ESTATE while the
     * state is not EDGE_SYS_RUNNING. */
    if (edge_sys_start(&sys) != EDGE_OK) {
        return 3;
    }

    /*
     * The reference firmware's main loop is interrupt-driven and never returns. This one steps
     * the scheduler a bounded number of times and then powers down - the shape the example
     * product uses - so the same binary is runnable as a host smoke test as well as cross-built
     * for the part. The interrupt-driven loop and the timer behind it are D2's and D4's.
     */
    for (unsigned i = 0u; i < 1000u; ++i) {
        if (edge_sys_step(&sys) != EDGE_OK) {
            (void)edge_sys_power_off(&sys);
            return 20;
        }
    }

    if (edge_sys_power_off(&sys) != EDGE_OK) {
        return 21;
    }
    if (edge_sys_deinit(&sys) != EDGE_OK) {
        return 22;
    }
    (void)motor_config_deinit(config);

    return 0;
}
