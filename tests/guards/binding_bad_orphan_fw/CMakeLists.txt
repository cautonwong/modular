set(EDGE_SYS_TARGET_demo sys_demo)
set(EDGE_BOARD_TARGET_demo board_demo)
set(EDGE_LEGAL_FAMILY_BOARD "demo:demo")

edge_add_product(demo family demo board demo infra flash apps alpha)
# Negative: firmware references a product that was never registered.
edge_add_arm_firmware(ghost_fw ghost cortex-m4 demo.ld)
