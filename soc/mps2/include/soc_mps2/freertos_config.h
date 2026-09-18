#ifndef SOC_MPS2_FREERTOS_CONFIG_H
#define SOC_MPS2_FREERTOS_CONFIG_H

/*
 * SoC-scoped FreeRTOS configuration (D87): MPS2 AN386 is a Cortex-M4, so the
 * interrupt priority encoding is a property of the SoC, not of the product.
 */
#define configPRIO_BITS 4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY                                                            \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY                                                       \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#endif
