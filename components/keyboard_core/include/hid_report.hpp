#pragma once

#include <array>
#include <cstdint>

namespace remote_hid {

constexpr std::size_t kHidKeySlots = 6;

struct HidReport {
    uint8_t modifiers = 0;
    uint8_t reserved = 0;
    std::array<uint8_t, kHidKeySlots> keys{};

    bool operator==(const HidReport& other) const {
        return modifiers == other.modifiers && keys == other.keys;
    }

    bool operator!=(const HidReport& other) const { return !(*this == other); }
};

} // namespace remote_hid
