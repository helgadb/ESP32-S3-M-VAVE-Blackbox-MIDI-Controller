// main.c — ESP32-S3 USB HOST / USB DEVICE MIDI Controller
// Compatible with esp_tinyusb (Device Mode) + USB Host Stack
// DEVICE mode chosen by holding GPIO6 low at boot.
// All MIDI transmissions routed via midi_tx_router.c

#include "globals.h"
#include "midi_storage.h"
#include "oled_display.h"
#include "navigation.h"
#include "midi_buttons.h"
#include "power_management.h"
#include "usb_daemon.h"
#include "midi_class_driver_txrx.h"
#include "midi_device_tx.h"
#include "midi_tx_router.h"
#include "battery_monitor.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "tinyusb.h"     // esp_tinyusb core
#include "tusb.h"  // MIDI class
#include "esp_timer.h"

static const char *TAG = "MAIN";

// =======================================================
//   TASK DA BATERIA 
// =======================================================
static void battery_monitoring_task(void *arg)
{    
    while (1) {
        battery_monitor_update();  // lê e atualiza o display
        vTaskDelay(pdMS_TO_TICKS(600000));  // espera 10 min.        
    }
}

void usb_debug_task(void *arg)
{
    while (1) {
        ESP_LOGI("TINYUSB", "tud_ready=%d | tud_midi_mounted=%d",
                 tud_ready(), tud_midi_mounted());

        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

// GPIO que seleciona o modo (0 = DEVICE, 1 = HOST)
#define MODE_BUTTON GPIO_NUM_6

// =======================================================
//   USB MIDI DEVICE DESCRIPTORS (obrigatórios no S3)
// =======================================================

enum {
    ITF_NUM_MIDI = 0,
    ITF_NUM_MIDI_STREAMING,
    ITF_COUNT
};

enum {
    EPNUM_MIDI = 1
};

#define TUSB_DESCRIPTOR_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

static const char *s_str_desc[] = {
    (char[]){0x09, 0x04},     // 0: Idioma (0x0409)
    "Espressif Systems",      // 1: Manufacturer
    "ESP32-S3 MIDI Device",   // 2: Product
    "0001",                   // 3: Serial
    "MIDI Interface",         // 4: Interface Name
};

// Full-speed configuration descriptor (obrigatório!)
static const uint8_t s_midi_cfg_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, TUSB_DESCRIPTOR_TOTAL_LEN, 0, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI, (0x80 | EPNUM_MIDI), 64),
};



// =======================================================
//  APP MAIN
// =======================================================

void app_main(void)
{ 
    ESP_LOGI(TAG, "=== INICIANDO BLACKBOX MIDI CONTROLLER ===");
    ESP_LOGI(TAG, "Firmware versao: 1.0");
    ESP_LOGI(TAG, "Compilado em: %s %s", __DATE__, __TIME__);
    
    ESP_LOGI(TAG, "Booting controller…");

    init_nvs();

    // --- Configura GPIOs ---
    gpio_config_t io6 = {
        .pin_bit_mask = 1ULL << GPIO_NUM_6,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&io6);

    gpio_config_t io17 = {
        .pin_bit_mask = 1ULL << GPIO_NUM_17,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&io17);

    // --- Verifica botões ---
    bool pressed_6  = (gpio_get_level(GPIO_NUM_6)  == 0);
    bool pressed_17 = (gpio_get_level(GPIO_NUM_17) == 0);

    if (pressed_6) {
        current_usb_mode = USB_MODE_DEVICE;
        save_usb_mode(USB_MODE_DEVICE);
        ESP_LOGW(TAG, "Boot override: DEVICE mode selected and saved.");
    }
    else if (pressed_17) {
        current_usb_mode = USB_MODE_HOST;
        save_usb_mode(USB_MODE_HOST);
        ESP_LOGW(TAG, "Boot override: HOST mode selected and saved.");
    }
    else {
        current_usb_mode = load_usb_mode();
        ESP_LOGW(TAG, "Boot from NVS: %s",
                current_usb_mode == USB_MODE_DEVICE ? "DEVICE" : "HOST");
    }

    // ------------------------------------
    // Inicializações comuns aos dois modos
    // ------------------------------------
    init_power_management();
    
    load_midi_commands();
    battery_monitor_init();
    g_battery_percent = battery_monitor_get_percentage();
    ESP_LOGI(TAG, "Valor inicial da bateria: %d%%", g_battery_percent);

    init_navigation_buttons();
    init_oled();
    battery_show_warning_once();
    
    xTaskCreatePinnedToCore(battery_monitoring_task, "battery_monitor", 4096, NULL, 1, NULL, 1);
    display_on = true;

    // ===================================================
    //                  USB DEVICE MODE
    // ===================================================
    if (current_usb_mode) {

        ESP_LOGW(TAG, "========= ENTERING USB DEVICE MODE =========");
        ESP_LOGI(TAG, "Inicializando TinyUSB…");

        // Configuração mínima obrigatória
        const tinyusb_config_t tusb_cfg = {
            .device_descriptor       = NULL,
            .string_descriptor       = s_str_desc,
            .configuration_descriptor = s_midi_cfg_desc,
            .external_phy            = false
        };

        esp_err_t err = tinyusb_driver_install(&tusb_cfg);
        ESP_ERROR_CHECK(err);

        ESP_LOGI(TAG, "tinyusb_driver_install OK");

        // Tasks da aplicação
        xTaskCreatePinnedToCore(button_check_task, "buttons", 4096, NULL, 3, NULL, 1);
        xTaskCreatePinnedToCore(navigation_button_task, "navigation", 4096, NULL, 3, NULL, 1);
        xTaskCreatePinnedToCore(power_management_task, "pwr_mgmt", 4096, NULL, 1, NULL, 1);

        // LOG EXTRA PARA DEBUG
        xTaskCreatePinnedToCore(
            usb_debug_task,
            "usb_debug",
            4096,
            NULL,
            2,
            NULL,
            1
        );

        ESP_LOGW(TAG, "USB Device Mode ativo. Aguarde o PC montar o dispositivo.");

        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // ===================================================
    //                  USB HOST MODE
    // ===================================================
    ESP_LOGW(TAG, "========= ENTERING USB HOST MODE =========");

    SemaphoreHandle_t sem = xSemaphoreCreateBinary();

    xTaskCreatePinnedToCore(host_lib_daemon_task, "daemon", 4096, sem, 2, NULL, 0);
    xTaskCreatePinnedToCore(class_driver_task, "usb_class", 8192, sem, 3, NULL, 0);

    xTaskCreatePinnedToCore(button_check_task, "buttons", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(navigation_button_task, "navigation", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(power_management_task, "pwr_mgmt", 4096, NULL, 1, NULL, 1);

    ESP_LOGI(TAG, "USB Host Mode ready.");

    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}