#include <stdlib.h>

/* A registered exemption: the marker is on a line that really allocates, and the
 * registry names it (#259). */
void demo_alloc(void **out) {
    *out = malloc(8); /* N5-allow: the demo fixture needs one allocation */
}
