idf_component_register(
    SRCS "main.c"
         "globals.c"
         "midi_storage.c"
         "oled_display.c"
         "navigation.c"
         "midi_buttons.c"
         "power_management.c"
         "usb_daemon.c"
         "midi_class_driver_txrx.c"
    REQUIRES freertos driver esp_timer ssd1306 nvs_flash esp_pm
    INCLUDE_DIRS "."
    PRIV_REQUIRES usb
)
