#include "key_map.hpp"

#include <cstddef>

namespace remote_hid {
namespace {

struct KeyNameEntry {
    const char* name;
    KeyCode key;
};

// Keep this table as the single source of truth for public key names. The
// order also controls the canonical name returned in status responses.
constexpr KeyNameEntry kKeyNames[] = {
    {"A", KeyCode::A},
    {"B", KeyCode::B},
    {"C", KeyCode::C},
    {"D", KeyCode::D},
    {"E", KeyCode::E},
    {"F", KeyCode::F},
    {"G", KeyCode::G},
    {"H", KeyCode::H},
    {"I", KeyCode::I},
    {"J", KeyCode::J},
    {"K", KeyCode::K},
    {"L", KeyCode::L},
    {"M", KeyCode::M},
    {"N", KeyCode::N},
    {"O", KeyCode::O},
    {"P", KeyCode::P},
    {"Q", KeyCode::Q},
    {"R", KeyCode::R},
    {"S", KeyCode::S},
    {"T", KeyCode::T},
    {"U", KeyCode::U},
    {"V", KeyCode::V},
    {"W", KeyCode::W},
    {"X", KeyCode::X},
    {"Y", KeyCode::Y},
    {"Z", KeyCode::Z},
    {"1", KeyCode::DIGIT_1},
    {"2", KeyCode::DIGIT_2},
    {"3", KeyCode::DIGIT_3},
    {"4", KeyCode::DIGIT_4},
    {"5", KeyCode::DIGIT_5},
    {"6", KeyCode::DIGIT_6},
    {"7", KeyCode::DIGIT_7},
    {"8", KeyCode::DIGIT_8},
    {"9", KeyCode::DIGIT_9},
    {"0", KeyCode::DIGIT_0},
    {"ENTER", KeyCode::ENTER},
    {"ESC", KeyCode::ESC},
    {"BACKSPACE", KeyCode::BACKSPACE},
    {"TAB", KeyCode::TAB},
    {"SPACE", KeyCode::SPACE},
    {"MINUS", KeyCode::MINUS},
    {"EQUAL", KeyCode::EQUAL},
    {"LEFT_BRACKET", KeyCode::LEFT_BRACKET},
    {"RIGHT_BRACKET", KeyCode::RIGHT_BRACKET},
    {"BACKSLASH", KeyCode::BACKSLASH},
    {"NON_US_HASH", KeyCode::NON_US_HASH},
    {"SEMICOLON", KeyCode::SEMICOLON},
    {"APOSTROPHE", KeyCode::APOSTROPHE},
    {"GRAVE", KeyCode::GRAVE},
    {"COMMA", KeyCode::COMMA},
    {"DOT", KeyCode::DOT},
    {"SLASH", KeyCode::SLASH},
    {"CAPS_LOCK", KeyCode::CAPS_LOCK},
    {"F1", KeyCode::F1},
    {"F2", KeyCode::F2},
    {"F3", KeyCode::F3},
    {"F4", KeyCode::F4},
    {"F5", KeyCode::F5},
    {"F6", KeyCode::F6},
    {"F7", KeyCode::F7},
    {"F8", KeyCode::F8},
    {"F9", KeyCode::F9},
    {"F10", KeyCode::F10},
    {"F11", KeyCode::F11},
    {"F12", KeyCode::F12},
    {"PRINT_SCREEN", KeyCode::PRINT_SCREEN},
    {"SCROLL_LOCK", KeyCode::SCROLL_LOCK},
    {"PAUSE", KeyCode::PAUSE},
    {"INSERT", KeyCode::INSERT},
    {"HOME", KeyCode::HOME},
    {"PAGE_UP", KeyCode::PAGE_UP},
    {"DELETE", KeyCode::DELETE},
    {"END", KeyCode::END},
    {"PAGE_DOWN", KeyCode::PAGE_DOWN},
    {"RIGHT", KeyCode::RIGHT},
    {"LEFT", KeyCode::LEFT},
    {"DOWN", KeyCode::DOWN},
    {"UP", KeyCode::UP},
    {"NUM_LOCK", KeyCode::NUM_LOCK},
    {"KEYPAD_SLASH", KeyCode::KEYPAD_SLASH},
    {"KEYPAD_ASTERISK", KeyCode::KEYPAD_ASTERISK},
    {"KEYPAD_MINUS", KeyCode::KEYPAD_MINUS},
    {"KEYPAD_PLUS", KeyCode::KEYPAD_PLUS},
    {"KEYPAD_ENTER", KeyCode::KEYPAD_ENTER},
    {"KEYPAD_1", KeyCode::KEYPAD_1},
    {"KEYPAD_2", KeyCode::KEYPAD_2},
    {"KEYPAD_3", KeyCode::KEYPAD_3},
    {"KEYPAD_4", KeyCode::KEYPAD_4},
    {"KEYPAD_5", KeyCode::KEYPAD_5},
    {"KEYPAD_6", KeyCode::KEYPAD_6},
    {"KEYPAD_7", KeyCode::KEYPAD_7},
    {"KEYPAD_8", KeyCode::KEYPAD_8},
    {"KEYPAD_9", KeyCode::KEYPAD_9},
    {"KEYPAD_0", KeyCode::KEYPAD_0},
    {"KEYPAD_DOT", KeyCode::KEYPAD_DOT},
    {"LEFT_CTRL", KeyCode::LEFT_CTRL},
    {"LEFT_SHIFT", KeyCode::LEFT_SHIFT},
    {"LEFT_ALT", KeyCode::LEFT_ALT},
    {"LEFT_GUI", KeyCode::LEFT_GUI},
    {"RIGHT_CTRL", KeyCode::RIGHT_CTRL},
    {"RIGHT_SHIFT", KeyCode::RIGHT_SHIFT},
    {"RIGHT_ALT", KeyCode::RIGHT_ALT},
    {"RIGHT_GUI", KeyCode::RIGHT_GUI},

    // Ergonomic aliases. Canonical status output remains LEFT_* / RIGHT_*.
    {"CTRL", KeyCode::LEFT_CTRL},
    {"CONTROL", KeyCode::LEFT_CTRL},
    {"SHIFT", KeyCode::LEFT_SHIFT},
    {"ALT", KeyCode::LEFT_ALT},
    {"GUI", KeyCode::LEFT_GUI},
    {"CMD", KeyCode::LEFT_GUI},
    {"WIN", KeyCode::LEFT_GUI},
    {"WINDOWS", KeyCode::LEFT_GUI},
};

bool equal_ignore_case(const char* lhs, const char* rhs) {
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }
    while (*lhs != '\0' && *rhs != '\0') {
        char left = *lhs++;
        char right = *rhs++;
        if (left >= 'a' && left <= 'z') {
            left = static_cast<char>(left - ('a' - 'A'));
        }
        if (right >= 'a' && right <= 'z') {
            right = static_cast<char>(right - ('a' - 'A'));
        }
        if (left != right) {
            return false;
        }
    }
    return *lhs == '\0' && *rhs == '\0';
}

} // namespace

bool key_from_name(const char* name, KeyCode& key) {
    if (name == nullptr || *name == '\0') {
        return false;
    }
    for (const KeyNameEntry& entry : kKeyNames) {
        if (equal_ignore_case(name, entry.name)) {
            key = entry.key;
            return true;
        }
    }
    return false;
}

const char* key_name(KeyCode key) {
    for (const KeyNameEntry& entry : kKeyNames) {
        if (entry.key == key) {
            return entry.name;
        }
    }
    return nullptr;
}

bool is_valid_key(KeyCode key) { return key_name(key) != nullptr; }

} // namespace remote_hid
