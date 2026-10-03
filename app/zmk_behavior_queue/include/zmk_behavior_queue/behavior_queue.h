#ifndef ZMK_BEHAVIOR_QUEUE_H
#define ZMK_BEHAVIOR_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZMK_BEHAVIOR_QUEUE_MAX_ITEMS 32

typedef struct zmk_behavior_queue_item {
    uint16_t behavior_id;
    uint32_t param1;
    uint32_t param2;
    uint32_t position;
    uint32_t wait_ms;
    bool press;
} zmk_behavior_queue_item_t;

typedef struct zmk_behavior_queue_sink_if {
    void *self;
    edge_status_t (*invoke_binding)(void *self, uint16_t behavior_id, uint32_t param1,
                                    uint32_t param2, bool press, uint32_t position,
                                    uint32_t timestamp_ms);
} zmk_behavior_queue_sink_if_t;

typedef struct zmk_behavior_queue_app {
    edge_module_t module;
    zmk_behavior_queue_sink_if_t sink;
    zmk_behavior_queue_item_t items[ZMK_BEHAVIOR_QUEUE_MAX_ITEMS];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    uint32_t next_run_time_ms;
    bool waiting;
} zmk_behavior_queue_app_t;

void zmk_behavior_queue_construct(zmk_behavior_queue_app_t *app, uint32_t module_id,
                                  uint8_t priority, const zmk_behavior_queue_sink_if_t *sink);

edge_status_t zmk_behavior_queue_init(zmk_behavior_queue_app_t *app);

edge_status_t zmk_behavior_queue_add(zmk_behavior_queue_app_t *app, uint16_t behavior_id,
                                     uint32_t param1, uint32_t param2, bool press,
                                     uint32_t position, uint32_t wait_ms, uint32_t timestamp_ms);

edge_status_t zmk_behavior_queue_process(zmk_behavior_queue_app_t *app, uint32_t current_time_ms);

void zmk_behavior_queue_clear(zmk_behavior_queue_app_t *app);

bool zmk_behavior_queue_is_empty(const zmk_behavior_queue_app_t *app);

uint16_t zmk_behavior_queue_count(const zmk_behavior_queue_app_t *app);

#ifdef __cplusplus
}
#endif

#endif
