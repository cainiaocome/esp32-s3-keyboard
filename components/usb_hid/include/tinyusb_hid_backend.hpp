#pragma once

#include "hid_backend.hpp"

namespace remote_hid {

class TinyUsbHidBackend final : public HidBackend {
  public:
    bool begin();
    bool send_report(const HidReport& report) override;
    bool mounted() const;

  private:
    bool initialized_ = false;
};

} // namespace remote_hid
