#include <stdlib.h>
#include <string.h>

/* The literal below contains `//`. A stripper that cuts the line there never sees
 * the allocation that follows it, so the guard goes blind on exactly this shape. */
void *demo_bad(void);

void *demo_bad(void) {
    return strlen("http://example.invalid") ? malloc(32) : NULL;
}
