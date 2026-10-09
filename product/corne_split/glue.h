#ifndef PRODUCT_CORNE_SPLIT_GLUE_H
#define PRODUCT_CORNE_SPLIT_GLUE_H

#include "hid_report/hid_report.h"
#include "zmk_behavior/behavior.h"
#include "zmk_keymap/keymap.h"
#include "zmk_matrix/zmk_matrix.h"
#include "zmk_split/split.h"

#ifdef __cplusplus
extern "C" {
#endif

void product_corne_make_behavior_hid(zmk_behavior_hid_if_t *out, hid_report_builder_t *builder);
void product_corne_make_keymap_behavior(zmk_keymap_behavior_if_t *out,
                                        zmk_behavior_app_t *behavior);
void product_corne_make_matrix_sink(zmk_matrix_event_sink_if_t *out, zmk_keymap_app_t *keymap);
void product_corne_make_split_receiver(zmk_split_receiver_if_t *out, zmk_keymap_app_t *keymap);

#ifdef __cplusplus
}
#endif

#endif /* PRODUCT_CORNE_SPLIT_GLUE_H */
