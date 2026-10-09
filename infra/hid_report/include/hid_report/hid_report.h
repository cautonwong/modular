#ifndef HID_REPORT_H
#define HID_REPORT_H

#include "edge/errors.h"
#include "hid_report/hid_keycodes.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HID_REPORT_KEYBOARD_6KRO_MAX 6u
#define HID_REPORT_KEYBOARD_NKRO_BYTES 32u
#define HID_REPORT_CONSUMER_MAX 4u

/* Standard Boot Protocol Keyboard Report (8 bytes) */
typedef struct hid_keyboard_report_boot {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[HID_REPORT_KEYBOARD_6KRO_MAX];
} hid_keyboard_report_boot_t;

/* NKRO Keyboard Bitmap Report (Modifiers + 32 bytes bitmap = 256 keys) */
typedef struct hid_keyboard_report_nkro {
    uint8_t modifiers;
    uint8_t bitmap[HID_REPORT_KEYBOARD_NKRO_BYTES];
} hid_keyboard_report_nkro_t;

/* Consumer Report */
typedef struct hid_consumer_report {
    uint16_t keys[HID_REPORT_CONSUMER_MAX];
} hid_consumer_report_t;

/* Mouse / Pointing Report */
typedef struct hid_mouse_report {
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
    int8_t pan;
} hid_mouse_report_t;

/* Unified HID Report Builder State */
typedef struct hid_report_builder {
    uint8_t modifiers;
    uint8_t boot_keys[HID_REPORT_KEYBOARD_6KRO_MAX];
    uint8_t nkro_bitmap[HID_REPORT_KEYBOARD_NKRO_BYTES];
    uint16_t consumer_keys[HID_REPORT_CONSUMER_MAX];
    hid_mouse_report_t mouse_report;

    bool boot_dirty;
    bool nkro_dirty;
    bool consumer_dirty;
    bool mouse_dirty;
} hid_report_builder_t;

void hid_report_builder_init(hid_report_builder_t *self);
void hid_report_builder_clear(hid_report_builder_t *self);

/* Keyboard operations */
edge_status_t hid_report_press_keycode(hid_report_builder_t *self, uint8_t keycode);
edge_status_t hid_report_release_keycode(hid_report_builder_t *self, uint8_t keycode);
edge_status_t hid_report_set_modifiers(hid_report_builder_t *self, uint8_t modifiers);
edge_status_t hid_report_add_modifiers(hid_report_builder_t *self, uint8_t modifiers);
edge_status_t hid_report_remove_modifiers(hid_report_builder_t *self, uint8_t modifiers);
bool hid_report_is_keycode_pressed(const hid_report_builder_t *self, uint8_t keycode);

/* Consumer operations */
edge_status_t hid_report_press_consumer(hid_report_builder_t *self, uint16_t consumer_code);
edge_status_t hid_report_release_consumer(hid_report_builder_t *self, uint16_t consumer_code);
bool hid_report_is_consumer_pressed(const hid_report_builder_t *self, uint16_t consumer_code);

/* Mouse operations */
edge_status_t hid_report_press_mouse_button(hid_report_builder_t *self, uint8_t button);
edge_status_t hid_report_release_mouse_button(hid_report_builder_t *self, uint8_t button);
edge_status_t hid_report_mouse_move(hid_report_builder_t *self, int8_t dx, int8_t dy, int8_t wheel,
                                    int8_t pan);

/* Serialization to wire buffers */
size_t hid_report_serialize_boot(const hid_report_builder_t *self, uint8_t *out_buf,
                                 size_t max_len);
size_t hid_report_serialize_nkro(const hid_report_builder_t *self, uint8_t *out_buf,
                                 size_t max_len);
size_t hid_report_serialize_consumer(const hid_report_builder_t *self, uint8_t *out_buf,
                                     size_t max_len);
size_t hid_report_serialize_mouse(const hid_report_builder_t *self, uint8_t *out_buf,
                                  size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* HID_REPORT_H */
