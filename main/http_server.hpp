#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_http_server.h"
#include "keyboard_engine.hpp"

struct cJSON;

namespace remote_hid {

struct StatusSnapshot {
    bool wifi_connected = false;
    bool usb_mounted = false;
    uint64_t uptime_ms = 0;
};

using StatusProvider = StatusSnapshot (*)(void* context);

class HttpServer final {
  public:
    HttpServer(KeyboardEngine& keyboard, const char* api_token, StatusProvider status_provider,
               void* status_context);
    ~HttpServer();

    bool start();
    void stop();

  private:
    static constexpr std::size_t kMaxRequestBody = 1024;
    static constexpr uint64_t kMaxDurationMs = 1000;

    static esp_err_t handle_status(httpd_req_t* req);
    static esp_err_t handle_key_down(httpd_req_t* req);
    static esp_err_t handle_key_up(httpd_req_t* req);
    static esp_err_t handle_key_press(httpd_req_t* req);
    static esp_err_t handle_release_all(httpd_req_t* req);
    static esp_err_t handle_combo(httpd_req_t* req);
    static esp_err_t handle_websocket(httpd_req_t* req);
    static esp_err_t websocket_pre_handshake(httpd_req_t* req);
    static void session_close(httpd_handle_t handle, int socket_fd);
    static void free_global_context(void* context);

    esp_err_t status(httpd_req_t* req);
    esp_err_t key_action(httpd_req_t* req, bool down);
    esp_err_t key_press(httpd_req_t* req);
    esp_err_t release_all(httpd_req_t* req);
    esp_err_t combo(httpd_req_t* req);
    esp_err_t websocket(httpd_req_t* req);

    bool authorized(httpd_req_t* req) const;
    bool require_authorized(httpd_req_t* req) const;
    bool read_json(httpd_req_t* req, cJSON*& root) const;
    bool parse_key(cJSON* root, KeyCode& key) const;
    bool parse_duration(cJSON* root, uint64_t& duration_ms) const;
    bool send_error(httpd_req_t* req, int status, const char* error, const char* message) const;
    bool send_success(httpd_req_t* req) const;
    bool send_ws_error(httpd_req_t* req, const char* error, const char* message) const;
    bool send_ws_success(httpd_req_t* req) const;
    bool send_ws_json(httpd_req_t* req, const char* json) const;
    bool dispatch_websocket_message(httpd_req_t* req, cJSON* root);

    KeyboardEngine& keyboard_;
    const char* api_token_;
    StatusProvider status_provider_;
    void* status_context_;
    httpd_handle_t server_ = nullptr;
};

} // namespace remote_hid
