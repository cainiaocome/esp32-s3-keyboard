#pragma once

#include <atomic>

#include "esp_event.h"

#include "app_config.hpp"
#include "keyboard_engine.hpp"

namespace remote_hid {

class WifiManager final {
public:
    WifiManager(const AppConfig& config, KeyboardEngine& keyboard);

    bool start();
    bool connected() const { return connected_.load(); }

private:
    static void event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data);
    void on_event(esp_event_base_t event_base, int32_t event_id,
                  void* event_data);

    const AppConfig& config_;
    KeyboardEngine& keyboard_;
    std::atomic<bool> connected_{false};
    bool started_ = false;
};

}  // namespace remote_hid
