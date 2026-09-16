#pragma once

#include <cstdint>

namespace remote_hid {

// USB HID Usage Tables keyboard page values. Modifier usages are represented
// by their usage values but are serialized into the report modifier bitmap.
enum class KeyCode : uint8_t {
    A = 0x04,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    DIGIT_1,
    DIGIT_2,
    DIGIT_3,
    DIGIT_4,
    DIGIT_5,
    DIGIT_6,
    DIGIT_7,
    DIGIT_8,
    DIGIT_9,
    DIGIT_0,
    ENTER,
    ESC,
    BACKSPACE,
    TAB,
    SPACE,
    MINUS,
    EQUAL,
    LEFT_BRACKET,
    RIGHT_BRACKET,
    BACKSLASH,
    NON_US_HASH,
    SEMICOLON,
    APOSTROPHE,
    GRAVE,
    COMMA,
    DOT,
    SLASH,
    CAPS_LOCK,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    PRINT_SCREEN,
    SCROLL_LOCK,
    PAUSE,
    INSERT,
    HOME,
    PAGE_UP,
    DELETE,
    END,
    PAGE_DOWN,
    RIGHT,
    LEFT,
    DOWN,
    UP,
    NUM_LOCK,
    KEYPAD_SLASH,
    KEYPAD_ASTERISK,
    KEYPAD_MINUS,
    KEYPAD_PLUS,
    KEYPAD_ENTER,
    KEYPAD_1,
    KEYPAD_2,
    KEYPAD_3,
    KEYPAD_4,
    KEYPAD_5,
    KEYPAD_6,
    KEYPAD_7,
    KEYPAD_8,
    KEYPAD_9,
    KEYPAD_0,
    KEYPAD_DOT,
    LEFT_CTRL = 0xE0,
    LEFT_SHIFT,
    LEFT_ALT,
    LEFT_GUI,
    RIGHT_CTRL,
    RIGHT_SHIFT,
    RIGHT_ALT,
    RIGHT_GUI,
};

constexpr uint8_t key_usage(KeyCode key) { return static_cast<uint8_t>(key); }

constexpr bool is_modifier(KeyCode key) {
    const uint8_t usage = key_usage(key);
    return usage >= key_usage(KeyCode::LEFT_CTRL) && usage <= key_usage(KeyCode::RIGHT_GUI);
}

constexpr uint8_t modifier_bit(KeyCode key) {
    return is_modifier(key)
               ? static_cast<uint8_t>(1U << (key_usage(key) - key_usage(KeyCode::LEFT_CTRL)))
               : 0;
}

} // namespace remote_hid
