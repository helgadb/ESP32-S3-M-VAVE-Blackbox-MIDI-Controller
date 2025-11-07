#include "power_management.h"
#include "globals.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "ssd1306.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "PWR";

void init_power_management(void)
{
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 40,
        .light_sleep_enable = false
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
}

void set_cpu_power_save_mode(void)
{
    ESP_LOGI(TAG, "Entering power save mode");
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 40,
        .light_sleep_enable = true
    };
    esp_pm_configure(&pm_config);
    cpu_power_save_mode = true;
}

void set_cpu_full_performance_mode(void)
{
    ESP_LOGI(TAG, "Entering full performance mode");
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 40,
        .light_sleep_enable = false
    };
    esp_pm_configure(&pm_config);
    cpu_power_save_mode = false;
}

void display_power_save(bool enable)
{
    if (!display_initialized) return;

    if (enable) {
        ssd1306_clear_screen(&dev, false);
        update_display_partial();
        display_on = true;
        ESP_LOGI(TAG, " Display ON - Standby exited");
    } else {
        ssd1306_clear_screen(&dev, false);
        ssd1306_display_text(&dev, 3, "   STANDBY...   ", 16, false);
        display_on = false;
        ESP_LOGI(TAG, " Display OFF - Standby mode");
    }
}

void update_cpu_activity_time(void) {
    last_cpu_activity_time = xTaskGetTickCount();
}

void update_display_activity_time(void) {
    last_display_activity_time = xTaskGetTickCount();
    if (!display_on) {
        display_power_save(true);
    }
}

void power_management_task(void *arg)
{
    ESP_LOGI(TAG, "=== SEPARATED CONTROL - CPU vs DISPLAY ===");

    last_cpu_activity_time = xTaskGetTickCount();
    last_display_activity_time = xTaskGetTickCount();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        uint32_t current_time = xTaskGetTickCount();
        uint32_t cpu_inactive_ms = (current_time - last_cpu_activity_time) * portTICK_PERIOD_MS;
        uint32_t display_inactive_ms = (current_time - last_display_activity_time) * portTICK_PERIOD_MS;

        ESP_LOGI(TAG, "[CONTROL] CPU: %d ms, Display: %d ms, CPU-Save: %s, Display-On: %s",
                 cpu_inactive_ms, display_inactive_ms,
                 cpu_power_save_mode ? "YES" : "NO",
                 display_on ? "YES" : "NO");

        if (!cpu_power_save_mode && cpu_inactive_ms > 10000) {
            ESP_LOGI(TAG, "💡 CPU POWER SAVE MODE ACTIVATED");
            set_cpu_power_save_mode();
        }

        if (display_inactive_ms > 15000 && display_on) {
            if (current_mode == MODE_EDIT) {
                ESP_LOGI(TAG, "🖥️ AUTO-CANCEL EDIT MODE (standby timeout)");
                current_mode = MODE_NORMAL;
                edit_initialized = false;
            }
            ESP_LOGI(TAG, "🖥️ DISPLAY STANDBY (no navigation)");
            display_power_save(false);
        }

        if (display_inactive_ms < 1000 && !display_on) {
            ESP_LOGI(TAG, "🖥️ DISPLAY REACTIVATED (navigation detected)");
            display_power_save(true);
        }

        if (cpu_inactive_ms < 1000 && cpu_power_save_mode) {
            ESP_LOGI(TAG, "💡 CPU POWER SAVE EXITED (activity detected)");
            set_cpu_full_performance_mode();
        }
    }
}
