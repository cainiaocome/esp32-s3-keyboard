#include "app_config.hpp"

#include "sdkconfig.h"

namespace remote_hid {

AppConfig load_app_config()
{
    return {
        CONFIG_REMOTE_HID_WIFI_SSID,
        CONFIG_REMOTE_HID_WIFI_PASSWORD,
        CONFIG_REMOTE_HID_API_TOKEN,
        static_cast<uint64_t>(CONFIG_REMOTE_HID_KEY_HOLD_TIMEOUT_MS),
        static_cast<uint64_t>(CONFIG_REMOTE_HID_KEY_PRESS_DURATION_MS),
    };
}

}  // namespace remote_hid
