#include "app_config.hpp"
#include "http_server.hpp"
#include "wifi_manager.hpp"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "tinyusb_hid_backend.hpp"

namespace remote_hid {
namespace {

constexpr char kTag[] = "remote_hid_app";
constexpr char kDiscoveryId[] = "esp32-s3-remote-hid-v1";

class EspTimerClock final : public Clock {
  public:
    uint64_t now_ms() const override { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }
};

struct Runtime {
    AppConfig config = load_app_config();
    EspTimerClock clock;
    TinyUsbHidBackend usb;
    KeyboardEngine keyboard;
    WifiManager wifi;
    const char* discovery_id = kDiscoveryId;
    HttpServer http;

    Runtime()
        : keyboard(
              usb, clock,
              KeyboardEngineConfig{config.key_hold_timeout_ms, config.key_press_duration_ms, 1000}),
          wifi(config, keyboard),
          http(keyboard, config.api_token, &Runtime::status_provider, this, discovery_id) {}

    static StatusSnapshot status_provider(void* context) {
        auto* runtime = static_cast<Runtime*>(context);
        return {runtime->wifi.connected(), runtime->usb.mounted(), runtime->clock.now_ms()};
    }
};

Runtime runtime;

void keyboard_maintenance_task(void* arg) {
    auto* current = static_cast<Runtime*>(arg);
    bool was_mounted = false;
    for (;;) {
        const bool mounted = current->usb.mounted();
        if (mounted != was_mounted) {
            if (mounted) {
                ESP_LOGI(kTag, "USB HID host attached; resetting keyboard state");
            } else if (was_mounted) {
                ESP_LOGW(kTag, "USB HID host detached; resetting keyboard state");
            }
            current->keyboard.release_all();
            was_mounted = mounted;
        }
        current->keyboard.tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

} // namespace
} // namespace remote_hid

extern "C" void app_main(void) {
    using namespace remote_hid;
    ESP_LOGI("remote_hid_app", "ESP32-S3 Remote HID boot");

    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);

    if (!runtime.usb.begin()) {
        ESP_LOGE("remote_hid_app", "USB HID initialization failed");
    }
    // Reset logical state at boot even if the USB host has not mounted yet.
    runtime.keyboard.release_all();

    if (runtime.wifi.start() && runtime.config.api_token != nullptr &&
        runtime.config.api_token[0] != '\0') {
        if (!runtime.http.start()) {
            ESP_LOGE("remote_hid_app", "HTTP API initialization failed");
        }
    } else if (runtime.config.api_token == nullptr || runtime.config.api_token[0] == '\0') {
        ESP_LOGW("remote_hid_app", "API token is not configured; HTTP API is disabled");
    }

    if (xTaskCreate(&keyboard_maintenance_task, "keyboard_maintenance", 3072, &runtime, 5,
                    nullptr) != pdPASS) {
        ESP_LOGE("remote_hid_app", "Could not start keyboard maintenance task");
        esp_restart();
    }
}
