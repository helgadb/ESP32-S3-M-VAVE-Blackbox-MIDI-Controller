#include "power_management.h"
#include "globals.h"
#include "esp_pm.h"
#include "ssd1306.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
        if (!display_on) {
            display_on = true;
            update_display_partial();
        }
    } else {
        if (display_on) {
            display_on = false;
            update_screen_by_state();
        }
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
    last_cpu_activity_time = xTaskGetTickCount();
    last_display_activity_time = xTaskGetTickCount();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        uint32_t current_time = xTaskGetTickCount();
        uint32_t cpu_inactive_ms = (current_time - last_cpu_activity_time) * portTICK_PERIOD_MS;
        uint32_t display_inactive_ms = (current_time - last_display_activity_time) * portTICK_PERIOD_MS;

        if (!cpu_power_save_mode && cpu_inactive_ms > 10000) {
            set_cpu_power_save_mode();
        }

        if (display_inactive_ms > 15000 && display_on) {
            if (current_mode == MODE_EDIT) {
                current_mode = MODE_NORMAL;
                edit_initialized = false;
            }
            display_power_save(false);
        }

        if (display_inactive_ms < 1000 && !display_on) {
            display_power_save(true);
        }

        if (cpu_inactive_ms < 1000 && cpu_power_save_mode) {
            set_cpu_full_performance_mode();
        }
    }
}