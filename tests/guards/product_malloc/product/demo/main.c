/* The composition root is not authorised to allocate: it instantiates every object
 * in the system, so a heap call here would be the one place a heap could return
 * unnoticed (D21, #257). */
#include <stdlib.h>

int main(void) {
    char *buffer = malloc(16);
    free(buffer);
    return 0;
}
