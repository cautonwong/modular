# Negative fixtures for the product combinator. Included only when
# EDGE_MODULE_NEGATIVE_CASE is set; each case must make CMake configure fail.
if(EDGE_MODULE_NEGATIVE_CASE STREQUAL "family")
    edge_add_product(negative_family family nonexistent board example infra flash apps dlt645)
elseif(EDGE_MODULE_NEGATIVE_CASE STREQUAL "board")
    edge_add_product(negative_board family example board nonexistent infra flash apps dlt645)
elseif(EDGE_MODULE_NEGATIVE_CASE STREQUAL "app")
    edge_add_product(negative_app family example board example infra flash apps nonexistent)
elseif(EDGE_MODULE_NEGATIVE_CASE STREQUAL "infra")
    edge_add_product(negative_infra family example board example infra nonexistent apps dlt645)
elseif(EDGE_MODULE_NEGATIVE_CASE STREQUAL "combo")
    # Both names exist, but example-family on the mps2 board is not a legal pair.
    edge_add_product(negative_combo family example board mps2 infra flash apps dlt645)
elseif(EDGE_MODULE_NEGATIVE_CASE STREQUAL "rebind")
    # D86: re-registering an existing product on a different (but legal) board must fail.
    edge_add_product(meter_mps2 family meter board example infra flash apps dlt645)
elseif(EDGE_MODULE_NEGATIVE_CASE STREQUAL "duplicate")
    # D86: a product name may only be registered once, even with the same board.
    edge_add_product(meter_mps2 family meter board mps2 infra flash gpio apps dlt645 relay)
else()
    message(FATAL_ERROR "unknown EDGE_MODULE_NEGATIVE_CASE='${EDGE_MODULE_NEGATIVE_CASE}'")
endif()
