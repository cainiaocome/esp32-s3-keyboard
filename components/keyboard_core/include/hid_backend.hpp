#pragma once

#include "hid_report.hpp"

namespace remote_hid {

class HidBackend {
  public:
    virtual ~HidBackend() = default;
    virtual bool send_report(const HidReport& report) = 0;
};

} // namespace remote_hid
