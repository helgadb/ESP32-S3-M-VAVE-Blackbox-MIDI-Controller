idf_component_register(
    SRCS "main.c"
          REQUIRES freertos driver esp_timer ssd1306 nvs_flash esp_pm
    INCLUDE_DIRS "."    
    PRIV_REQUIRES usb 

)