# D88: boards and families declare themselves in their own area directories.
set(EDGE_LEGAL_FAMILY_BOARD "demo:other")

# Negative: demo:demo is not in the legal family/board whitelist.
edge_add_product(demo family demo board demo infra flash apps alpha)
