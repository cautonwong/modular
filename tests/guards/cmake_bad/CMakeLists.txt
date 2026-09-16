add_library(app_alpha STATIC alpha.c)
add_library(app_beta STATIC beta.c)
edge_add_product(demo family demo board demo apps alpha beta)
