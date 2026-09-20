/*
 * Fixture for check_pal_arch_binding.py: the same skeleton as the real Cortex-M
 * port with the refusal removed. A non-ARM target would compile this happily and
 * link a fake clock, so the checker must reject this tree.
 */
#if defined(__arm__)
int edge_pal_cortex_m_fixture_native(void) {
    return 1;
}
#elif defined(EDGE_PAL_CORTEX_M_HOST_TEST)
int edge_pal_cortex_m_fixture_host(void) {
    return 2;
}
#endif
