# OS model: idle, yield and sleep as seen by sys. No kernel here.
edge_add_pal(os SOURCES src/idle.c DEPS edge_module)
