#include "edge/event.h"

int main(void) {
    edge_event_sink_t event_sink;
    watch_ui_init(&ui, &disp, &status, &event_sink);
    ble_weather_init(&weather, &time_port, &event_sink);
    return 0;
}
