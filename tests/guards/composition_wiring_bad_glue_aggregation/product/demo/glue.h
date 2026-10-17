#ifndef DEMO_GLUE_H
#define DEMO_GLUE_H

#include "heart_rate/heart_rate.h"
#include "step_counter/step_counter.h"
#include "watch_time/watch_time.h"

typedef struct demo_glue_state {
    watch_time_t *time_app;
    step_counter_t *step_app;
    heart_rate_t *hr_app;
} demo_glue_state_t;

#endif
