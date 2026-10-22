#include "edge/event.h"

/* The call is commented out, so nothing is assembled with a NULL sink. The gate
 * used to read raw source and report this as a violation, whose only workaround was
 * deleting the comment (#262 finding 3). */
int main(void) {
    edge_event_sink_t event_sink;
    /* ble_weather_init(&weather, &time_port, NULL); */
    (void)event_sink;
    return 0;
}
