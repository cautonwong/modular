# Bare-metal Cortex-M PAL (D46/D85): PRIMASK critical sections, DSB barrier,
# SysTick monotonic time, IPSR ISR detection. A host fallback keeps the target
# analyzable and host-testable off target.
edge_add_pal(cortex_m_bare SOURCES src/pal_cortex_m.c DEPS edge_module)
