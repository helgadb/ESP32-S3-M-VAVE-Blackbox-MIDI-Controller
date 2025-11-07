#include "globals.h"
#include "midi_storage.h"
#include "oled_display.h"
#include "navigation.h"
#include "midi_buttons.h"
#include "power_management.h"
#include "usb_daemon.h"
#include "midi_class_driver_txrx.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "freertos/semphr.h"

static const char *TAG = "DAEMON";

#define DAEMON_TASK_PRIORITY    2
#define CLASS_TASK_PRIORITY     3

void app_main(void)
{
    ESP_LOGI(TAG, "Starting system (modularized)");

    init_power_management();
    last_cpu_activity_time = xTaskGetTickCount();
    last_display_activity_time = xTaskGetTickCount();
    ESP_LOGI(TAG, "Separated timers initialized");
    ESP_LOGI(TAG, "Activity timer initialized to: %d", last_cpu_activity_time);

    init_nvs();
    load_midi_commands();

    SemaphoreHandle_t signaling_sem = xSemaphoreCreateBinary();

    init_oled();
    display_on = true;
    init_navigation_buttons();

    TaskHandle_t daemon_task_hdl;
    TaskHandle_t class_driver_task_hdl;
    TaskHandle_t button_task_hdl;
    TaskHandle_t navigation_task_hdl;
    TaskHandle_t power_management_hdl;

    xTaskCreatePinnedToCore(host_lib_daemon_task, "daemon", 4096, (void *)signaling_sem, DAEMON_TASK_PRIORITY, &daemon_task_hdl, 0);
    xTaskCreatePinnedToCore(class_driver_task, "class", 4096, (void *)signaling_sem, CLASS_TASK_PRIORITY, &class_driver_task_hdl, 0);
    xTaskCreatePinnedToCore(button_check_task, "button", 4096, NULL, CLASS_TASK_PRIORITY, &button_task_hdl, 1);
    xTaskCreatePinnedToCore(navigation_button_task, "navigation", 4096, NULL, CLASS_TASK_PRIORITY, &navigation_task_hdl, 1);
    if (xTaskCreatePinnedToCore(power_management_task, "pwr_mgmt", 4096, NULL, 1, &power_management_hdl, 1) == pdPASS) {
        ESP_LOGI(TAG, "Power management task CREATED SUCCESSFULLY");
    } else {
        ESP_LOGE(TAG, "FAILED to create power management task");
    }

    ESP_LOGI(TAG, "System started - Power Management Debug Mode");

    for (int i = 0; i < BUTTON_COUNT; i++) {
        ESP_LOGI(TAG, "Button %d: %02X %02X %02X %02X", i + 1,
                current_commands[i].data[0], current_commands[i].data[1],
                current_commands[i].data[2], current_commands[i].data[3]);
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
