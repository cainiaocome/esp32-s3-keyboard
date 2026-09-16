#include "http_server.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "cJSON.h"
#include "esp_log.h"

#include "key_map.hpp"

namespace remote_hid {
namespace {

constexpr char kTag[] = "remote_hid_http";
constexpr char kSuccessJson[] = "{\"ok\":true}";
constexpr char kBearerPrefix[] = "Bearer ";

const char* result_error(EngineResult result) {
    switch (result) {
    case EngineResult::kInvalidKey:
        return "unknown_key";
    case EngineResult::kRollover:
        return "rollover_limit";
    case EngineResult::kInvalidArgument:
        return "invalid_argument";
    case EngineResult::kQueueFull:
        return "operation_queue_full";
    case EngineResult::kBackendFailure:
        return "hid_backend_failure";
    case EngineResult::kOk:
        return nullptr;
    }
    return "internal_error";
}

int result_status(EngineResult result) {
    switch (result) {
    case EngineResult::kRollover:
        return 422;
    case EngineResult::kQueueFull:
    case EngineResult::kBackendFailure:
        return 503;
    case EngineResult::kInvalidKey:
    case EngineResult::kInvalidArgument:
        return 400;
    case EngineResult::kOk:
        return 200;
    }
    return 500;
}

const char* result_message(EngineResult result) {
    switch (result) {
    case EngineResult::kInvalidKey:
        return "The requested key is not supported";
    case EngineResult::kRollover:
        return "Too many normal keys are held simultaneously";
    case EngineResult::kInvalidArgument:
        return "The keyboard operation is invalid";
    case EngineResult::kQueueFull:
        return "The keyboard operation queue is full";
    case EngineResult::kBackendFailure:
        return "The USB HID backend could not accept the report";
    case EngineResult::kOk:
        return "ok";
    }
    return "Internal keyboard error";
}

bool constant_time_equal(const char* lhs, std::size_t lhs_len, const char* rhs,
                         std::size_t rhs_len) {
    const std::size_t length = lhs_len > rhs_len ? lhs_len : rhs_len;
    unsigned int difference = static_cast<unsigned int>(lhs_len ^ rhs_len);
    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char left = i < lhs_len ? static_cast<unsigned char>(lhs[i]) : 0;
        const unsigned char right = i < rhs_len ? static_cast<unsigned char>(rhs[i]) : 0;
        difference |= static_cast<unsigned int>(left ^ right);
    }
    return difference == 0;
}

bool json_string(cJSON* root, const char* name, const char*& value) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsString(item) || item->valuestring == nullptr || item->valuestring[0] == '\0') {
        return false;
    }
    value = item->valuestring;
    return true;
}

} // namespace

HttpServer::HttpServer(KeyboardEngine& keyboard, const char* api_token,
                       StatusProvider status_provider, void* status_context)
    : keyboard_(keyboard), api_token_(api_token), status_provider_(status_provider),
      status_context_(status_context) {}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::start() {
    if (server_ != nullptr) {
        return true;
    }
    if (api_token_ == nullptr || api_token_[0] == '\0') {
        ESP_LOGE(kTag, "API token is not configured; refusing to start HTTP server");
        return false;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = 8192;
    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start HTTP server");
        server_ = nullptr;
        return false;
    }

    httpd_uri_t status_uri = {};
    status_uri.uri = "/api/v1/status";
    status_uri.method = HTTP_GET;
    status_uri.handler = &HttpServer::handle_status;
    status_uri.user_ctx = this;

    httpd_uri_t down_uri = {};
    down_uri.uri = "/api/v1/key/down";
    down_uri.method = HTTP_POST;
    down_uri.handler = &HttpServer::handle_key_down;
    down_uri.user_ctx = this;

    httpd_uri_t up_uri = {};
    up_uri.uri = "/api/v1/key/up";
    up_uri.method = HTTP_POST;
    up_uri.handler = &HttpServer::handle_key_up;
    up_uri.user_ctx = this;

    httpd_uri_t press_uri = {};
    press_uri.uri = "/api/v1/key/press";
    press_uri.method = HTTP_POST;
    press_uri.handler = &HttpServer::handle_key_press;
    press_uri.user_ctx = this;

    httpd_uri_t release_uri = {};
    release_uri.uri = "/api/v1/key/release-all";
    release_uri.method = HTTP_POST;
    release_uri.handler = &HttpServer::handle_release_all;
    release_uri.user_ctx = this;

    httpd_uri_t combo_uri = {};
    combo_uri.uri = "/api/v1/key/combo";
    combo_uri.method = HTTP_POST;
    combo_uri.handler = &HttpServer::handle_combo;
    combo_uri.user_ctx = this;

    const httpd_uri_t* routes[] = {&status_uri, &down_uri,    &up_uri,
                                   &press_uri,  &release_uri, &combo_uri};
    for (const httpd_uri_t* route : routes) {
        if (httpd_register_uri_handler(server_, route) != ESP_OK) {
            ESP_LOGE(kTag, "Failed to register HTTP route %s", route->uri);
            stop();
            return false;
        }
    }

    httpd_uri_t websocket_uri = {};
    websocket_uri.uri = "/ws/v1/keyboard";
    websocket_uri.method = HTTP_GET;
    websocket_uri.handler = &HttpServer::handle_websocket;
    websocket_uri.user_ctx = this;
    websocket_uri.is_websocket = true;
    websocket_uri.handle_ws_control_frames = true;
#if CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
    websocket_uri.ws_pre_handshake_cb = &HttpServer::websocket_pre_handshake;
#endif
    if (httpd_register_uri_handler(server_, &websocket_uri) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to register WebSocket route");
        stop();
        return false;
    }

    ESP_LOGI(kTag, "HTTP API started on port %d", config.server_port);
    return true;
}

void HttpServer::stop() {
    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
        keyboard_.release_all();
        ESP_LOGI(kTag, "HTTP server stopped; keyboard state released");
    }
}

esp_err_t HttpServer::handle_status(httpd_req_t* req) {
    return static_cast<HttpServer*>(req->user_ctx)->status(req);
}

esp_err_t HttpServer::handle_key_down(httpd_req_t* req) {
    return static_cast<HttpServer*>(req->user_ctx)->key_action(req, true);
}

esp_err_t HttpServer::handle_key_up(httpd_req_t* req) {
    return static_cast<HttpServer*>(req->user_ctx)->key_action(req, false);
}

esp_err_t HttpServer::handle_key_press(httpd_req_t* req) {
    return static_cast<HttpServer*>(req->user_ctx)->key_press(req);
}

esp_err_t HttpServer::handle_release_all(httpd_req_t* req) {
    return static_cast<HttpServer*>(req->user_ctx)->release_all(req);
}

esp_err_t HttpServer::handle_combo(httpd_req_t* req) {
    return static_cast<HttpServer*>(req->user_ctx)->combo(req);
}

esp_err_t HttpServer::handle_websocket(httpd_req_t* req) {
    return static_cast<HttpServer*>(req->user_ctx)->websocket(req);
}

#if CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
esp_err_t HttpServer::websocket_pre_handshake(httpd_req_t* req) {
    auto* server = static_cast<HttpServer*>(req->user_ctx);
    if (server->authorized(req)) {
        return ESP_OK;
    }
    server->send_error(req, 401, "unauthorized", "A valid Bearer token is required");
    return ESP_FAIL;
}
#endif

bool HttpServer::authorized(httpd_req_t* req) const {
    const size_t header_length = httpd_req_get_hdr_value_len(req, "Authorization");
    if (header_length <= sizeof(kBearerPrefix) - 1 || header_length > 256) {
        return false;
    }
    char header[257] = {};
    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) != ESP_OK) {
        return false;
    }
    const std::size_t token_length = std::strlen(api_token_);
    const std::size_t supplied_length = header_length - (sizeof(kBearerPrefix) - 1);
    return std::strncmp(header, kBearerPrefix, sizeof(kBearerPrefix) - 1) == 0 &&
           constant_time_equal(header + sizeof(kBearerPrefix) - 1, supplied_length, api_token_,
                               token_length);
}

bool HttpServer::require_authorized(httpd_req_t* req) const {
    if (authorized(req)) {
        return true;
    }
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
    send_error(req, 401, "unauthorized", "A valid Bearer token is required");
    return false;
}

bool HttpServer::read_json(httpd_req_t* req, cJSON*& root) const {
    root = nullptr;
    if (req->content_len == 0) {
        send_error(req, 400, "missing_body", "A JSON request body is required");
        return false;
    }
    if (req->content_len > kMaxRequestBody) {
        send_error(req, 413, "body_too_large", "The request body is too long");
        return false;
    }

    std::unique_ptr<char[]> body(new (std::nothrow) char[req->content_len + 1]);
    if (!body) {
        send_error(req, 500, "internal_error", "Unable to allocate request buffer");
        return false;
    }
    size_t received = 0;
    while (received < req->content_len) {
        const int result = httpd_req_recv(req, body.get() + received, req->content_len - received);
        if (result <= 0) {
            send_error(req, 400, "request_read_failed", "Unable to read request body");
            return false;
        }
        received += static_cast<size_t>(result);
    }
    body[received] = '\0';
    root = cJSON_ParseWithLength(body.get(), received);
    if (!root || !cJSON_IsObject(root)) {
        if (root) {
            cJSON_Delete(root);
            root = nullptr;
        }
        send_error(req, 400, "invalid_json", "Request body must be a JSON object");
        return false;
    }
    return true;
}

bool HttpServer::parse_key(cJSON* root, KeyCode& key) const {
    const char* name = nullptr;
    if (!json_string(root, "key", name) || !key_from_name(name, key)) {
        return false;
    }
    return true;
}

bool HttpServer::parse_duration(cJSON* root, uint64_t& duration_ms) const {
    duration_ms = 0;
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "duration_ms");
    if (item == nullptr) {
        return true;
    }
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) || item->valuedouble < 1 ||
        item->valuedouble > kMaxDurationMs || std::floor(item->valuedouble) != item->valuedouble) {
        return false;
    }
    duration_ms = static_cast<uint64_t>(item->valuedouble);
    return true;
}

bool HttpServer::send_error(httpd_req_t* req, int status, const char* error,
                            const char* message) const {
    cJSON* response = cJSON_CreateObject();
    if (!response) {
        return false;
    }
    cJSON_AddBoolToObject(response, "ok", false);
    cJSON_AddStringToObject(response, "error", error);
    cJSON_AddStringToObject(response, "message", message);
    char* payload = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (!payload) {
        return false;
    }
    char status_line[32] = {};
    std::snprintf(status_line, sizeof(status_line), "%d Error", status);
    httpd_resp_set_status(req, status_line);
    httpd_resp_set_type(req, "application/json");
    const esp_err_t result = httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
    cJSON_free(payload);
    return result == ESP_OK;
}

bool HttpServer::send_success(httpd_req_t* req) const {
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, kSuccessJson, HTTPD_RESP_USE_STRLEN) == ESP_OK;
}

esp_err_t HttpServer::status(httpd_req_t* req) {
    if (!require_authorized(req)) {
        return ESP_OK;
    }
    cJSON* response = cJSON_CreateObject();
    cJSON* keys = cJSON_CreateArray();
    if (!response || !keys) {
        cJSON_Delete(response);
        cJSON_Delete(keys);
        send_error(req, 500, "internal_error", "Unable to allocate status response");
        return ESP_OK;
    }

    KeyCode pressed[14] = {};
    const std::size_t count = keyboard_.pressed_keys(pressed, 14);
    for (std::size_t i = 0; i < count && i < 14; ++i) {
        const char* name = key_name(pressed[i]);
        if (name) {
            cJSON_AddItemToArray(keys, cJSON_CreateString(name));
        }
    }
    const StatusSnapshot snapshot =
        status_provider_ ? status_provider_(status_context_) : StatusSnapshot{};
    cJSON_AddBoolToObject(response, "ok", true);
    cJSON_AddBoolToObject(response, "wifi_connected", snapshot.wifi_connected);
    cJSON_AddBoolToObject(response, "usb_mounted", snapshot.usb_mounted);
    cJSON_AddItemToObject(response, "pressed_keys", keys);
    cJSON_AddNumberToObject(response, "uptime_ms", static_cast<double>(snapshot.uptime_ms));
    char* payload = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (!payload) {
        send_error(req, 500, "internal_error", "Unable to serialize status response");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
    cJSON_free(payload);
    return ESP_OK;
}

esp_err_t HttpServer::key_action(httpd_req_t* req, bool down) {
    if (!require_authorized(req)) {
        return ESP_OK;
    }
    cJSON* root = nullptr;
    if (!read_json(req, root)) {
        return ESP_OK;
    }
    KeyCode key{};
    const bool parsed = parse_key(root, key);
    cJSON_Delete(root);
    if (!parsed) {
        send_error(req, 400, "unknown_key", "Request must contain a supported key");
        return ESP_OK;
    }
    const EngineResult result = down ? keyboard_.key_down(key) : keyboard_.key_up(key);
    if (result != EngineResult::kOk) {
        send_error(req, result_status(result), result_error(result), result_message(result));
        return ESP_OK;
    }
    send_success(req);
    return ESP_OK;
}

esp_err_t HttpServer::key_press(httpd_req_t* req) {
    if (!require_authorized(req)) {
        return ESP_OK;
    }
    cJSON* root = nullptr;
    if (!read_json(req, root)) {
        return ESP_OK;
    }
    KeyCode key{};
    uint64_t duration_ms = 0;
    const bool parsed_key = parse_key(root, key);
    const bool parsed_duration = parse_duration(root, duration_ms);
    cJSON_Delete(root);
    if (!parsed_key) {
        send_error(req, 400, "unknown_key", "Request must contain a supported key");
        return ESP_OK;
    }
    if (!parsed_duration) {
        send_error(req, 400, "invalid_duration", "duration_ms must be an integer from 1 to 1000");
        return ESP_OK;
    }
    const EngineResult result = keyboard_.key_press(key, duration_ms);
    if (result != EngineResult::kOk) {
        send_error(req, result_status(result), result_error(result), result_message(result));
        return ESP_OK;
    }
    send_success(req);
    return ESP_OK;
}

esp_err_t HttpServer::release_all(httpd_req_t* req) {
    if (!require_authorized(req)) {
        return ESP_OK;
    }
    const EngineResult result = keyboard_.release_all();
    if (result != EngineResult::kOk) {
        send_error(req, result_status(result), result_error(result), result_message(result));
        return ESP_OK;
    }
    send_success(req);
    return ESP_OK;
}

esp_err_t HttpServer::combo(httpd_req_t* req) {
    if (!require_authorized(req)) {
        return ESP_OK;
    }
    cJSON* root = nullptr;
    if (!read_json(req, root)) {
        return ESP_OK;
    }
    cJSON* items = cJSON_GetObjectItemCaseSensitive(root, "keys");
    uint64_t duration_ms = 0;
    if (!cJSON_IsArray(items) || cJSON_GetArraySize(items) == 0 ||
        cJSON_GetArraySize(items) > static_cast<int>(kHidKeySlots) ||
        !parse_duration(root, duration_ms)) {
        cJSON_Delete(root);
        send_error(req, 400, "invalid_combo",
                   "keys must contain 1 to 6 keys and duration_ms must be 1 to 1000");
        return ESP_OK;
    }
    KeyCode keys[kHidKeySlots] = {};
    const int count = cJSON_GetArraySize(items);
    for (int i = 0; i < count; ++i) {
        cJSON* item = cJSON_GetArrayItem(items, i);
        if (!cJSON_IsString(item) || item->valuestring == nullptr ||
            !key_from_name(item->valuestring, keys[i])) {
            cJSON_Delete(root);
            send_error(req, 400, "unknown_key", "Combo contains an unsupported key");
            return ESP_OK;
        }
    }
    cJSON_Delete(root);
    const EngineResult result = keyboard_.combo(keys, static_cast<std::size_t>(count), duration_ms);
    if (result != EngineResult::kOk) {
        send_error(req, result_status(result), result_error(result), result_message(result));
        return ESP_OK;
    }
    send_success(req);
    return ESP_OK;
}

bool HttpServer::send_ws_json(httpd_req_t* req, const char* json) const {
    httpd_ws_frame_t packet = {};
    packet.type = HTTPD_WS_TYPE_TEXT;
    packet.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(json));
    packet.len = std::strlen(json);
    return httpd_ws_send_frame(req, &packet) == ESP_OK;
}

bool HttpServer::send_ws_error(httpd_req_t* req, const char* error, const char* message) const {
    cJSON* response = cJSON_CreateObject();
    if (!response) {
        return false;
    }
    cJSON_AddBoolToObject(response, "ok", false);
    cJSON_AddStringToObject(response, "error", error);
    cJSON_AddStringToObject(response, "message", message);
    char* payload = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (!payload) {
        return false;
    }
    const bool result = send_ws_json(req, payload);
    cJSON_free(payload);
    return result;
}

bool HttpServer::send_ws_success(httpd_req_t* req) const { return send_ws_json(req, kSuccessJson); }

bool HttpServer::dispatch_websocket_message(httpd_req_t* req, cJSON* root) {
    const char* type = nullptr;
    if (!json_string(root, "type", type)) {
        return send_ws_error(req, "invalid_message", "Message type is required");
    }
    if (std::strcmp(type, "release_all") == 0) {
        const EngineResult result = keyboard_.release_all();
        if (result != EngineResult::kOk) {
            return send_ws_error(req, result_error(result), result_message(result));
        }
        return send_ws_success(req);
    }
    if (std::strcmp(type, "key_down") != 0 && std::strcmp(type, "key_up") != 0) {
        return send_ws_error(req, "unknown_command",
                             "Supported commands are key_down, key_up, release_all");
    }
    KeyCode key{};
    const char* key_value = nullptr;
    if (!json_string(root, "key", key_value) || !key_from_name(key_value, key)) {
        return send_ws_error(req, "unknown_key", "Message must contain a supported key");
    }
    const bool down = std::strcmp(type, "key_down") == 0;
    const EngineResult result = down ? keyboard_.key_down(key) : keyboard_.key_up(key);
    if (result != EngineResult::kOk) {
        return send_ws_error(req, result_error(result), result_message(result));
    }
    return send_ws_success(req);
}

esp_err_t HttpServer::websocket(httpd_req_t* req) {
    httpd_ws_frame_t packet = {};
    esp_err_t result = httpd_ws_recv_frame(req, &packet, 0);
    if (result != ESP_OK) {
        keyboard_.release_all();
        return result;
    }
    if (packet.len > kMaxRequestBody) {
        keyboard_.release_all();
        return send_ws_error(req, "body_too_large", "The WebSocket message is too long") ? ESP_OK
                                                                                         : ESP_FAIL;
    }
    std::unique_ptr<uint8_t[]> payload(new (std::nothrow) uint8_t[packet.len + 1]);
    if (!payload) {
        keyboard_.release_all();
        return ESP_ERR_NO_MEM;
    }
    packet.payload = payload.get();
    result = httpd_ws_recv_frame(req, &packet, packet.len);
    if (result != ESP_OK) {
        keyboard_.release_all();
        return result;
    }
    if (packet.type == HTTPD_WS_TYPE_CLOSE) {
        keyboard_.release_all();
        return ESP_OK;
    }
    if (packet.type == HTTPD_WS_TYPE_PING) {
        packet.type = HTTPD_WS_TYPE_PONG;
        return httpd_ws_send_frame(req, &packet);
    }
    if (packet.type != HTTPD_WS_TYPE_TEXT) {
        send_ws_error(req, "invalid_message", "Only text WebSocket messages are supported");
        return ESP_OK;
    }
    payload[packet.len] = '\0';
    cJSON* root = cJSON_ParseWithLength(reinterpret_cast<char*>(payload.get()), packet.len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        send_ws_error(req, "invalid_json", "Message must be a JSON object");
        return ESP_OK;
    }
    dispatch_websocket_message(req, root);
    cJSON_Delete(root);
    return ESP_OK;
}

} // namespace remote_hid
