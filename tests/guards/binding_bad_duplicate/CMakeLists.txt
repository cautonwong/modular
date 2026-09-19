# D88: boards and families declare themselves in their own area directories.
set(EDGE_LEGAL_FAMILY_BOARD "demo:demo;demo:alt")

# Negative: the same product name is registered twice (re-bound to another board).
edge_add_product(demo family demo board demo infra flash apps alpha)
edge_add_product(demo family demo board alt infra flash apps alpha)
