#include "globals.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

uint8_t g_battery_percent = 0;

const int button_gpios[BUTTONS_PER_PAGE] = {
    6, 7, 14, 15, 16, 17, 18, 21, 47, 48
};

const int BTN_UP_GPIO = 10;
const int BTN_DOWN_GPIO = 11;
const int BTN_HASH_GPIO = 12;
const int BTN_STAR_GPIO = 13;

const int I2C_SDA_GPIO = 8;
const int I2C_SCL_GPIO = 9;
const int I2C_RESET_GPIO = -1;
const int OLED_WIDTH = 128;
const int OLED_HEIGHT = 64;

midi_command_t current_commands[TOTAL_BUTTONS];

int current_page = 0;
int current_button = 0;
menu_mode_t current_mode = MODE_NORMAL;
int edit_byte_index = 0;
int edit_nibble_index = 0;
SSD1306_t dev;
bool display_initialized = false;
midi_command_t edit_command;
int scroll_offset = 0;
bool edit_initialized = false;

bool last_button_states[BUTTONS_PER_PAGE];
uint32_t button_press_start_ms[BUTTONS_PER_PAGE];
bool long_press_triggered[BUTTONS_PER_PAGE];

bool last_up_state = true;
bool last_down_state = true;
bool last_hash_state = true;
bool last_star_state = true;

uint32_t last_cpu_activity_time = 0;
uint32_t last_display_activity_time = 0;
bool display_on = true;
bool cpu_power_save_mode = false;

void init_button_states(void) {
    for (int i = 0; i < BUTTONS_PER_PAGE; i++) {
        last_button_states[i] = true;
        button_press_start_ms[i] = 0;
        long_press_triggered[i] = false;
    }
}

int get_absolute_button_index(int page, int button) {
    return page * BUTTONS_PER_PAGE + button;
}

void update_screen_by_state(void)
{
    if (!display_initialized) return;
    
    if (!display_on) {
        if (g_battery_percent <= LOW_BATTERY_THRESHOLD) {
            ssd1306_clear_screen(&dev, false);
            char line[17];
            memset(line, ' ', 16);
            strcpy(&line[4], "LOW");
            line[16] = '\0';
            ssd1306_display_text(&dev, 1, line, 16, false);
            
            memset(line, ' ', 16);
            char page_buf[3];
            snprintf(page_buf, sizeof(page_buf), "%d", current_page);
            int start_col = (16 - strlen(page_buf)) / 2;
            strcpy(&line[start_col], page_buf);
            
            for (int row = 3; row <= 4; row++) {
                ssd1306_display_text(&dev, row, line, 16, false);
            }
        } else {
            // Desenha número grande usando bitmap (função está em oled_display.c)
            extern void draw_big_number_standby(void);
            draw_big_number_standby();
        }
    } else {
        update_display_partial();
    }
}

void change_page(int delta) {
    int new_page = current_page + delta;
    
    if (new_page >= TOTAL_PAGES) {
        new_page = 0;
    } else if (new_page < 0) {
        new_page = TOTAL_PAGES - 1;
    }
    
    if (new_page != current_page) {
        current_page = new_page;
        current_button = 0;
        scroll_offset = 0;
        
        last_cpu_activity_time = xTaskGetTickCount();
        
        update_screen_by_state();
    }
}