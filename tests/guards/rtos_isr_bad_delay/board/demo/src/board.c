void vTaskDelay(unsigned int ticks);

void board_demo_irq_timer0(void) {
    vTaskDelay(10);
}
