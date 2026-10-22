#include "edge/event.h"

int demo_app_wrap(int a, int b) {
    return a + b;
}

/* A plain `split(",")` counted four arguments here, so the NULL at index 1 was
 * read as the nested call's second argument and the gate stayed green. */
int main(void) {
    demo_app_init(demo_app_wrap(1, 2), NULL);
    return 0;
}
