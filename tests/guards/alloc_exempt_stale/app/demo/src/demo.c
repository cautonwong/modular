#include <stdlib.h>

/* The marker sits on a line that allocates nothing: the call it used to excuse was
 * rewritten and nobody removed it. The gate must reject it (#259). */
void demo_stale(void **out) {
    *out = (void *)0; /* N5-allow: leftover marker */
}
