/* Negative fixture: an app depending on a heavy libc facility (N5). */
#include <stdlib.h>

void *alpha_alloc(unsigned n) {
    return malloc(n);
}
