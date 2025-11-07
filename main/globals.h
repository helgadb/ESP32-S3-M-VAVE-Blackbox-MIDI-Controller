#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ssd1306.h"

#define BUTTON_COUNT 10
#define VISIBLE_BUTTONS 5

extern const int button_gpios[BUTTON_COUNT];

extern const int BTN_UP_GPIO;
extern const int BTN_DOWN_GPIO;
extern const int BTN_HASH_GPIO;
extern const int BTN_STAR_GPIO;

extern const int I2C_SDA_GPIO;
extern const int I2C_SCL_GPIO;
extern const int I2C_RESET_GPIO;
extern const int OLED_WIDTH;
extern const int OLED_HEIGHT;

typedef struct {
    uint8_t data[4];
    char description[20];
} midi_command_t;

typedef enum {
    MODE_NORMAL,
    MODE_EDIT
} menu_mode_t;

extern midi_command_t current_commands[BUTTON_COUNT];
extern int current_button;
extern menu_mode_t current_mode;
extern int edit_byte_index;
extern int edit_nibble_index;
extern SSD1306_t dev;
extern bool display_initialized;
extern midi_command_t edit_command;
extern int scroll_offset;
extern bool edit_initialized;
extern bool last_up_state;
extern bool last_down_state;
extern bool last_hash_state;
extern bool last_star_state;
extern uint32_t last_cpu_activity_time;
extern uint32_t last_display_activity_time;
extern bool display_on;
extern bool cpu_power_save_mode;

void update_display_partial(void);
void save_midi_commands(void);
bool load_midi_commands(void);

bool midi_driver_ready_for_tx(void);
bool midi_send_data(const uint8_t *data, size_t length);
