#include "globals.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

const int button_gpios[BUTTON_COUNT] = {
    6,7,14,15,16,17,18,21,47,48
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

midi_command_t current_commands[BUTTON_COUNT];
int current_button = 0;
menu_mode_t current_mode = MODE_NORMAL;
int edit_byte_index = 0;
int edit_nibble_index = 0;
SSD1306_t dev;
bool display_initialized = false;
midi_command_t edit_command;

int scroll_offset = 0;
bool edit_initialized = false;

bool last_up_state = true;
bool last_down_state = true;
bool last_hash_state = true;
bool last_star_state = true;

uint32_t last_cpu_activity_time = 0;
uint32_t last_display_activity_time = 0;
bool display_on = true;
bool cpu_power_save_mode = false;
