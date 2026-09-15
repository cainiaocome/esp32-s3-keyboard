idf_component_register(
    SRCS "src/tinyusb_hid_backend.cpp"
    INCLUDE_DIRS "include"
    REQUIRES keyboard_core esp_tinyusb
)
