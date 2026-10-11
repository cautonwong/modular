#include "edge/desc.h"

static const uint32_t a_deps[] = {0x1100u};
static const edge_module_desc_t mod_a = {
    .id = 0x1000u,
    .name = "mod_a",
    .dependencies = a_deps,
    .dep_count = 1,
};

static const uint32_t b_deps[] = {0x1000u};
static const edge_module_desc_t mod_b = {
    .id = 0x1100u,
    .name = "mod_b",
    .dependencies = b_deps,
    .dep_count = 1,
};
