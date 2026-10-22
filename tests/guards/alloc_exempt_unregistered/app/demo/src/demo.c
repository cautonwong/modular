#include <stdlib.h>

/* A marker with no registry entry: the line allocates, the exemption is real, but
 * nothing records why (#259). */
void demo_unregistered(void **out) {
    *out = malloc(8); /* N5-allow: nobody registered this */
}
