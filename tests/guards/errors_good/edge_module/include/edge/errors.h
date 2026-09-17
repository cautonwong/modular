#ifndef ERRORS_H
#define ERRORS_H
typedef enum edge_status { EDGE_OK = 0, EDGE_EINVAL = -1, EDGE_EIO = -8 } edge_status_t;
#define EDGE_ERR(mod, code) (-(int)(((mod) & 0xFF00) | ((code) & 0xFF)))
#endif
