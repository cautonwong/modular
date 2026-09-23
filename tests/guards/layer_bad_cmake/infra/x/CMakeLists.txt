# Fixture: a declared dependency the matrix forbids (infra -> soc).
edge_add_infra(x SOURCES src/x.c DEPS edge_module soc_something)
