#include "midi_buttons.h"
#include "globals.h"
#include "esp_log.h"
#include "midi_class_driver_txrx.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "power_management.h"

static const char *TAG = "MIDI_BTN";

void init_midi_buttons(void)
{
    uint64_t button_mask = 0;
    for (int i = 0; i < BUTTON_COUNT; i++) {
        button_mask |= (1ULL << button_gpios[i]);
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = button_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "MIDI buttons initialized");
}

void button_check_task(void *arg)
{
    init_midi_buttons();

    ESP_LOGI(TAG, "Button controller ready");

    bool last_button_states[BUTTON_COUNT];
    uint32_t last_send_times[BUTTON_COUNT];

    for (int i = 0; i < BUTTON_COUNT; i++) {
        last_button_states[i] = true;
        last_send_times[i] = 0;
    }

    const uint32_t DEBOUNCE_DELAY = pdMS_TO_TICKS(100);

    while (1) {

        for (int i = 0; i < BUTTON_COUNT; i++) {
            bool current_state = gpio_get_level(button_gpios[i]);
            uint32_t current_time = xTaskGetTickCount();

            if (last_button_states[i] && !current_state) {
                if ((current_time - last_send_times[i]) >= DEBOUNCE_DELAY) {
                    update_cpu_activity_time();
                    ESP_LOGI(TAG, "MIDI Button %d - only CPU power timer updated", i+1);
                    if (cpu_power_save_mode) {
                        set_cpu_full_performance_mode();
                    }
                    if(display_on && current_mode == MODE_NORMAL){
                        if (current_button != i) {
                            current_button = i;
                            if (i < scroll_offset) {
                                scroll_offset = i;
                            } else if (i >= scroll_offset + VISIBLE_BUTTONS) {
                                scroll_offset = i - VISIBLE_BUTTONS + 1;
                            }
                            update_display_partial();
                        }
                    }

                    ESP_LOGI(TAG, "Button %d SENDING: %02X %02X %02X %02X", i + 1,
                            current_commands[i].data[0], current_commands[i].data[1],
                            current_commands[i].data[2], current_commands[i].data[3]);

                    if (midi_driver_ready_for_tx()) {
                        midi_send_data(current_commands[i].data, sizeof(current_commands[i].data));
                    } else {
                        ESP_LOGI(TAG, "Driver not ready");
                    }

                    last_send_times[i] = current_time;
                }
            }

            last_button_states[i] = current_state;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
