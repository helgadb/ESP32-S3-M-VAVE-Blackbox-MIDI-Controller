#include "navigation.h"
#include "globals.h"
#include "ssd1306.h"
#include <string.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_management.h"

void init_navigation_buttons(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BTN_UP_GPIO) | (1ULL << BTN_DOWN_GPIO) |
                       (1ULL << BTN_HASH_GPIO) | (1ULL << BTN_STAR_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}

void increment_nibble(uint8_t *byte, int nibble) {
    if (nibble == 0) {
        uint8_t high_nibble = (*byte >> 4) & 0x0F;
        high_nibble = (high_nibble + 1) & 0x0F;
        *byte = (high_nibble << 4) | (*byte & 0x0F);
    } else {
        uint8_t low_nibble = (*byte & 0x0F);
        low_nibble = (low_nibble + 1) & 0x0F;
        *byte = (*byte & 0xF0) | low_nibble;
    }
}

void decrement_nibble(uint8_t *byte, int nibble) {
    if (nibble == 0) {
        uint8_t high_nibble = (*byte >> 4) & 0x0F;
        high_nibble = (high_nibble - 1) & 0x0F;
        *byte = (high_nibble << 4) | (*byte & 0x0F);
    } else {
        uint8_t low_nibble = (*byte & 0x0F);
        low_nibble = (low_nibble - 1) & 0x0F;
        *byte = (*byte & 0xF0) | low_nibble;
    }
}

void handle_navigation(void)
{
    bool current_up = gpio_get_level(BTN_UP_GPIO);
    bool current_down = gpio_get_level(BTN_DOWN_GPIO);
    bool current_hash = gpio_get_level(BTN_HASH_GPIO);
    bool current_star = gpio_get_level(BTN_STAR_GPIO);

    if ((last_up_state && !current_up) ||
        (last_down_state && !current_down) ||
        (last_hash_state && !current_hash) ||
        (last_star_state && !current_star)) {

        // Botões de navegação: atualiza AMBOS os timers
        update_cpu_activity_time();
        update_display_activity_time();  // Isso acorda o display!
        
        extern void battery_restore_normal_screen(void);
        battery_restore_normal_screen();
    }

    if (last_up_state && !current_up) {
        switch (current_mode) {
            case MODE_NORMAL:
                if (current_button > 0) {
                    current_button--;
                    if (current_button < scroll_offset) {
                        scroll_offset = current_button;
                    }
                    update_display_partial();
                }
                break;
            case MODE_EDIT:
                increment_nibble(&edit_command.data[edit_byte_index], edit_nibble_index);
                update_display_partial();
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (last_down_state && !current_down) {
        switch (current_mode) {
            case MODE_NORMAL:
                if (current_button < BUTTONS_PER_PAGE - 1) {
                    current_button++;
                    if (current_button >= scroll_offset + VISIBLE_BUTTONS) {
                        scroll_offset = current_button - VISIBLE_BUTTONS + 1;
                    }
                    update_display_partial();
                }
                break;
            case MODE_EDIT:
                decrement_nibble(&edit_command.data[edit_byte_index], edit_nibble_index);
                update_display_partial();
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (last_star_state && !current_star) {
        switch (current_mode) {
            case MODE_NORMAL:
                current_mode = MODE_EDIT;
                edit_byte_index = 0;
                edit_nibble_index = 0;
                int abs_index = get_absolute_button_index(current_page, current_button);
                memcpy(edit_command.data, current_commands[abs_index].data, sizeof(edit_command.data));
                edit_initialized = false;
                update_display_partial();
                break;
            case MODE_EDIT:
                if (edit_nibble_index == 0) {
                    edit_nibble_index = 1;
                } else {
                    edit_nibble_index = 0;
                    edit_byte_index = (edit_byte_index + 1) % 4;
                }
                update_display_partial();
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (last_hash_state && !current_hash) {
        switch (current_mode) {
            case MODE_NORMAL:
                if (current_button != 0) {
                    current_button = 0;
                    scroll_offset = 0;
                    update_display_partial();
                }
                break;
            case MODE_EDIT:
                uint32_t press_start_time = xTaskGetTickCount();
                while (!gpio_get_level(BTN_HASH_GPIO)) {
                    if ((xTaskGetTickCount() - press_start_time) > pdMS_TO_TICKS(1000)) {
                        current_mode = MODE_NORMAL;
                        edit_initialized = false;
                        update_display_partial();
                        vTaskDelay(pdMS_TO_TICKS(300));
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                if ((xTaskGetTickCount() - press_start_time) <= pdMS_TO_TICKS(1000)) {
                    int abs_index = get_absolute_button_index(current_page, current_button);
                    memcpy(current_commands[abs_index].data, edit_command.data, sizeof(edit_command.data));
                    save_midi_commands();
                    current_mode = MODE_NORMAL;
                    edit_initialized = false;
                    update_display_partial();
                }
                break;
            default:
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    last_up_state = current_up;
    last_down_state = current_down;
    last_hash_state = current_hash;
    last_star_state = current_star;
}

void navigation_button_task(void *arg)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50));
        handle_navigation();
    }
}