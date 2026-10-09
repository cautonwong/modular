#ifndef HID_KEYCODES_H
#define HID_KEYCODES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HID Usage Pages */
#define HID_USAGE_PAGE_GENERIC_DESKTOP 0x01u
#define HID_USAGE_PAGE_KEYBOARD_KEYPAD 0x07u
#define HID_USAGE_PAGE_LEDS 0x08u
#define HID_USAGE_PAGE_CONSUMER 0x0Cu
#define HID_USAGE_PAGE_DIGITIZERS 0x0Du

/* Modifier bitmasks */
#define HID_MOD_LCTRL (1u << 0)
#define HID_MOD_LSHIFT (1u << 1)
#define HID_MOD_LALT (1u << 2)
#define HID_MOD_LGUI (1u << 3)
#define HID_MOD_RCTRL (1u << 4)
#define HID_MOD_RSHIFT (1u << 5)
#define HID_MOD_RALT (1u << 6)
#define HID_MOD_RGUI (1u << 7)

/* Common Keyboard Keycodes (Usage Page 0x07) */
#define HID_KEY_NONE 0x00u
#define HID_KEY_A 0x04u
#define HID_KEY_B 0x05u
#define HID_KEY_C 0x06u
#define HID_KEY_D 0x07u
#define HID_KEY_E 0x08u
#define HID_KEY_F 0x09u
#define HID_KEY_G 0x0Au
#define HID_KEY_H 0x0Bu
#define HID_KEY_I 0x0Cu
#define HID_KEY_J 0x0Du
#define HID_KEY_K 0x0Eu
#define HID_KEY_L 0x0Fu
#define HID_KEY_M 0x10u
#define HID_KEY_N 0x11u
#define HID_KEY_O 0x12u
#define HID_KEY_P 0x13u
#define HID_KEY_Q 0x14u
#define HID_KEY_R 0x15u
#define HID_KEY_S 0x16u
#define HID_KEY_T 0x17u
#define HID_KEY_U 0x18u
#define HID_KEY_V 0x19u
#define HID_KEY_W 0x1Au
#define HID_KEY_X 0x1Bu
#define HID_KEY_Y 0x1Cu
#define HID_KEY_Z 0x1Du
#define HID_KEY_1 0x1Eu
#define HID_KEY_2 0x1Fu
#define HID_KEY_3 0x20u
#define HID_KEY_4 0x21u
#define HID_KEY_5 0x22u
#define HID_KEY_6 0x23u
#define HID_KEY_7 0x24u
#define HID_KEY_8 0x25u
#define HID_KEY_9 0x26u
#define HID_KEY_0 0x27u
#define HID_KEY_ENTER 0x28u
#define HID_KEY_ESCAPE 0x29u
#define HID_KEY_BACKSPACE 0x2Au
#define HID_KEY_TAB 0x2Bu
#define HID_KEY_SPACE 0x2Cu
#define HID_KEY_MINUS 0x2Du
#define HID_KEY_EQUAL 0x2Eu
#define HID_KEY_LEFT_BRACKET 0x2Fu
#define HID_KEY_RIGHT_BRACKET 0x30u
#define HID_KEY_BACKSLASH 0x31u
#define HID_KEY_SEMICOLON 0x33u
#define HID_KEY_APOSTROPHE 0x34u
#define HID_KEY_GRAVE 0x35u
#define HID_KEY_COMMA 0x36u
#define HID_KEY_DOT 0x37u
#define HID_KEY_SLASH 0x38u
#define HID_KEY_CAPS_LOCK 0x39u
#define HID_KEY_F1 0x3Au
#define HID_KEY_F2 0x3Bu
#define HID_KEY_F3 0x3Cu
#define HID_KEY_F4 0x3Du
#define HID_KEY_F5 0x3Eu
#define HID_KEY_F6 0x3Fu
#define HID_KEY_F7 0x40u
#define HID_KEY_F8 0x41u
#define HID_KEY_F9 0x42u
#define HID_KEY_F10 0x43u
#define HID_KEY_F11 0x44u
#define HID_KEY_F12 0x45u
#define HID_KEY_PRINTSCREEN 0x46u
#define HID_KEY_SCROLLLOCK 0x47u
#define HID_KEY_PAUSE 0x48u
#define HID_KEY_INSERT 0x49u
#define HID_KEY_HOME 0x4Au
#define HID_KEY_PAGEUP 0x4Bu
#define HID_KEY_DELETE 0x4Cu
#define HID_KEY_END 0x4Du
#define HID_KEY_PAGEDOWN 0x4Eu
#define HID_KEY_RIGHT 0x4Fu
#define HID_KEY_LEFT 0x50u
#define HID_KEY_DOWN 0x51u
#define HID_KEY_UP 0x52u
#define HID_KEY_LCTRL 0xE0u
#define HID_KEY_LSHIFT 0xE1u
#define HID_KEY_LALT 0xE2u
#define HID_KEY_LGUI 0xE3u
#define HID_KEY_RCTRL 0xE4u
#define HID_KEY_RSHIFT 0xE5u
#define HID_KEY_RALT 0xE6u
#define HID_KEY_RGUI 0xE7u

/* Consumer Keys (Usage Page 0x0C) */
#define HID_CONSUMER_POWER 0x0030u
#define HID_CONSUMER_RESET 0x0031u
#define HID_CONSUMER_SLEEP 0x0032u
#define HID_CONSUMER_BRIGHTNESS_INC 0x006Fu
#define HID_CONSUMER_BRIGHTNESS_DEC 0x0070u
#define HID_CONSUMER_PLAY 0x00B0u
#define HID_CONSUMER_PAUSE 0x00B1u
#define HID_CONSUMER_RECORD 0x00B2u
#define HID_CONSUMER_FAST_FORWARD 0x00B3u
#define HID_CONSUMER_REWIND 0x00B4u
#define HID_CONSUMER_SCAN_NEXT 0x00B5u
#define HID_CONSUMER_SCAN_PREV 0x00B6u
#define HID_CONSUMER_STOP 0x00B7u
#define HID_CONSUMER_PLAY_PAUSE 0x00CDu
#define HID_CONSUMER_MUTE 0x00E2u
#define HID_CONSUMER_VOLUME_INC 0x00E9u
#define HID_CONSUMER_VOLUME_DEC 0x00EAu
#define HID_CONSUMER_AL_CALCULATOR 0x0192u
#define HID_CONSUMER_AC_SEARCH 0x0221u
#define HID_CONSUMER_AC_HOME 0x0223u
#define HID_CONSUMER_AC_BACK 0x0224u
#define HID_CONSUMER_AC_FORWARD 0x0225u
#define HID_CONSUMER_AC_REFRESH 0x0227u

/* Mouse Button masks */
#define HID_MOUSE_BTN_LEFT (1u << 0)
#define HID_MOUSE_BTN_RIGHT (1u << 1)
#define HID_MOUSE_BTN_MIDDLE (1u << 2)
#define HID_MOUSE_BTN_4 (1u << 3)
#define HID_MOUSE_BTN_5 (1u << 4)

#ifdef __cplusplus
}
#endif

#endif /* HID_KEYCODES_H */
