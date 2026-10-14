#include <stdlib.h>

void *bad_func(void) {
    /* VIOLATION: Calling malloc in app */
    return malloc(32);
}
