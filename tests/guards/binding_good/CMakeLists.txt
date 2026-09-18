# D86 positive fixture: one product, one board, one registration.
set(EDGE_SYS_TARGET_demo sys_demo)
set(EDGE_BOARD_TARGET_demo board_demo)
set(EDGE_LEGAL_FAMILY_BOARD "demo:demo;demo:alt")

edge_add_product(demo family demo board demo infra flash apps alpha)
edge_add_arm_firmware(demo_fw demo cortex-m4 demo.ld)
