#include "tinyusb_hid_backend.hpp"

#include <algorithm>
#include <cstdint>

#include "class/hid/hid_device.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"

namespace remote_hid {
namespace {

constexpr char kTag[] = "remote_hid_usb";

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

const uint8_t kHidReportDescriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(),
};

const char kLanguageDescriptor[] = {0x09, 0x04};
const char* kStringDescriptor[] = {
    kLanguageDescriptor,
    "Remote HID",
    "ESP32-S3 Remote Keyboard",
    "00000001",
    "Keyboard",
};

const uint8_t kConfigurationDescriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 4, true, sizeof(kHidReportDescriptor), 0x81, 16, 10),
};

}  // namespace

uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return kHidReportDescriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t* buffer,
                               uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const* buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

bool TinyUsbHidBackend::begin()
{
    if (initialized_) {
        return true;
    }
    tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG();
    config.descriptor.device = nullptr;
    config.descriptor.full_speed_config = kConfigurationDescriptor;
    config.descriptor.string = kStringDescriptor;
    config.descriptor.string_count =
        sizeof(kStringDescriptor) / sizeof(kStringDescriptor[0]);
#if (TUD_OPT_HIGH_SPEED)
    config.descriptor.high_speed_config = kConfigurationDescriptor;
#endif

    const esp_err_t result = tinyusb_driver_install(&config);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "TinyUSB initialization failed: %s", esp_err_to_name(result));
        return false;
    }
    initialized_ = true;
    ESP_LOGI(kTag, "TinyUSB HID keyboard initialized");
    return true;
}

bool TinyUsbHidBackend::send_report(const HidReport& report)
{
    if (!initialized_ || !tud_mounted()) {
        return false;
    }
    uint8_t keycodes[kHidKeySlots] = {};
    std::copy(report.keys.begin(), report.keys.end(), keycodes);
    return tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD,
                                   report.modifiers, keycodes);
}

bool TinyUsbHidBackend::mounted() const
{
    return initialized_ && tud_mounted();
}

}  // namespace remote_hid
