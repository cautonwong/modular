#ifndef EDGE_MODULES_H
#define EDGE_MODULES_H

/* Positive fixture: one block per layer, every entry inside its own block. */
#define EDGE_MODULE_IDS(X)                                                                         \
    X(ALPHA, 0x1000, app)                                                                          \
    X(BETA, 0x2000, app)

#define EDGE_MODULE_BLOCK_app_LO 0x1000u
#define EDGE_MODULE_BLOCK_app_HI 0x2FFFu

#endif
