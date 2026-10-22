#ifndef GUARD_WATCH_UI_H
#define GUARD_WATCH_UI_H

#include "edge/event.h"

typedef struct watch_ui_guard watch_ui_guard_t;
typedef struct watch_ui_disp_guard watch_ui_disp_guard_t;
typedef struct watch_ui_status_guard watch_ui_status_guard_t;

edge_status_t watch_ui_init(watch_ui_guard_t *self, watch_ui_disp_guard_t *disp,
                            watch_ui_status_guard_t *status, const edge_event_sink_t *sink);

#endif
