#ifndef EDGE_MODULES_H
#define EDGE_MODULES_H

/* Negative fixture: a value inside the infra block declared as an app module,
 * and a layer token that has no allocated block at all. */
#define EDGE_MODULE_IDS(X)                                                                         \
    X(ALPHA, 0x1000, app)                                                                          \
    X(BETA, 0x3000, app)                                                                           \
    X(GAMMA, 0x3100, driver)

#define EDGE_MODULE_BLOCK_app_LO 0x1000u
#define EDGE_MODULE_BLOCK_app_HI 0x2FFFu
#define EDGE_MODULE_BLOCK_infra_LO 0x3000u
#define EDGE_MODULE_BLOCK_infra_HI 0x3FFFu

#endif
