# Device driver. Register-level access lives in soc/<soc>/, adaptation to a
# port lives in the product's glue (D48).
edge_add_infra(flash SOURCES src/flash.c src/flash_emul.c DEPS edge_module)
