idf_component_register(
    SRCS 
        "globals.c"
        "main.c"
        "midi_buttons.c"
        "midi_class_driver_txrx.c"
        "midi_device_tx.c"
        "midi_storage.c"
        "midi_tx_router.c"
        "midi_uart.c"
        "navigation.c"
        "oled_display.c"
        "power_management.c"
        "usb_daemon.c"

    INCLUDE_DIRS 
        "."

    REQUIRES 
        freertos
        driver
        ssd1306
        nvs_flash
        esp_pm

    PRIV_REQUIRES 
        usb
        esp_timer
)