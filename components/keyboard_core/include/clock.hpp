#pragma once

#include <cstdint>

namespace remote_hid {

class Clock {
public:
    virtual ~Clock() = default;
    virtual uint64_t now_ms() const = 0;
};

}  // namespace remote_hid
