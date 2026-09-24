edge_add_app(vesc_comm
    SOURCES
        src/vesc_comm.c
        src/packet.c
        src/commands.c
    DEPS
        edge_module
)
