#pragma once

#include <cstdint>

namespace remote_hid {

struct AppConfig {
    const char* wifi_ssid;
    const char* wifi_password;
    const char* api_token;
    uint64_t key_hold_timeout_ms;
    uint64_t key_press_duration_ms;
};

AppConfig load_app_config();

}  // namespace remote_hid
