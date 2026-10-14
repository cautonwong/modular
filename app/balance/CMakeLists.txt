edge_add_app(balance
    SOURCES
        src/balance.c
    DEPS
        edge_module
)
target_link_libraries(app_balance PUBLIC m)
