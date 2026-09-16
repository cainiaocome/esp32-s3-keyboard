#include "wifi_manager.hpp"

#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"

namespace remote_hid {
namespace {
constexpr char kTag[] = "remote_hid_wifi";
}

WifiManager::WifiManager(const AppConfig& config, KeyboardEngine& keyboard)
    : config_(config), keyboard_(keyboard) {}

bool WifiManager::start() {
    if (started_) {
        return true;
    }
    if (config_.wifi_ssid == nullptr || config_.wifi_ssid[0] == '\0') {
        ESP_LOGW(kTag, "Wi-Fi SSID is not configured; network API is disabled");
        return false;
    }

    const esp_err_t netif_result = esp_netif_init();
    if (netif_result != ESP_OK && netif_result != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "Failed to initialize network interface");
        return false;
    }
    esp_err_t result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "Failed to create default event loop: %s", esp_err_to_name(result));
        return false;
    }
    if (esp_netif_create_default_wifi_sta() == nullptr) {
        ESP_LOGE(kTag, "Failed to create Wi-Fi station interface");
        return false;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&init_config);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Failed to initialize Wi-Fi: %s", esp_err_to_name(result));
        return false;
    }
    result =
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiManager::event_handler, this);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Failed to register Wi-Fi event handler: %s", esp_err_to_name(result));
        return false;
    }
    result = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiManager::event_handler,
                                        this);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Failed to register IP event handler: %s", esp_err_to_name(result));
        return false;
    }

    wifi_config_t wifi_config = {};
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), config_.wifi_ssid,
                 sizeof(wifi_config.sta.ssid) - 1);
    const char* password = config_.wifi_password == nullptr ? "" : config_.wifi_password;
    std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password), password,
                 sizeof(wifi_config.sta.password) - 1);
    // Refuse open/weak rogue APs advertising the configured SSID. WPA2 is the
    // minimum acceptable association for the bearer-token control channel.
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = true;

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config) != ESP_OK || esp_wifi_start() != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start Wi-Fi station");
        return false;
    }
    started_ = true;
    ESP_LOGI(kTag, "Wi-Fi station starting");
    return true;
}

void WifiManager::event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                                void* event_data) {
    static_cast<WifiManager*>(arg)->on_event(event_base, event_id, event_data);
}

void WifiManager::on_event(esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        connected_.store(false);
        // A network/session failure must never leave a target key held.
        keyboard_.release_all();
        esp_wifi_connect();
        ESP_LOGW(kTag, "Wi-Fi disconnected; keyboard state released and reconnecting");
        return;
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const auto* event = static_cast<const ip_event_got_ip_t*>(event_data);
        connected_.store(true);
        ESP_LOGI(kTag, "Wi-Fi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

} // namespace remote_hid
