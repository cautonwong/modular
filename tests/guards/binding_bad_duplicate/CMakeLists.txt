set(EDGE_SYS_TARGET_demo sys_demo)
set(EDGE_BOARD_TARGET_demo board_demo)
set(EDGE_BOARD_TARGET_alt board_alt)
set(EDGE_LEGAL_FAMILY_BOARD "demo:demo;demo:alt")

# Negative: the same product name is registered twice (re-bound to another board).
edge_add_product(demo family demo board demo infra flash apps alpha)
edge_add_product(demo family demo board alt infra flash apps alpha)
