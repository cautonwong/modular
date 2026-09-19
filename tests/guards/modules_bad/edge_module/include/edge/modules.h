#ifndef EDGE_MODULES_H
#define EDGE_MODULES_H

/* Negative fixture: a duplicate value and a misaligned value. */
#define EDGE_MODULE_IDS(X)                                                                         \
    X(ALPHA, 0x1000, app)                                                                          \
    X(BETA, 0x1000, app)                                                                           \
    X(GAMMA, 0x1050, app)

#define EDGE_MODULE_BLOCK_app_LO 0x1000u
#define EDGE_MODULE_BLOCK_app_HI 0x2FFFu

#endif
