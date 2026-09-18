set(EDGE_SYS_TARGET_demo sys_demo)
set(EDGE_BOARD_TARGET_demo board_demo)
set(EDGE_LEGAL_FAMILY_BOARD "demo:other")

# Negative: demo:demo is not in the legal family/board whitelist.
edge_add_product(demo family demo board demo infra flash apps alpha)
