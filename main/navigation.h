#pragma once
#include <stdint.h>

void init_navigation_buttons(void);
void navigation_button_task(void *arg);
void handle_navigation(void);
void increment_nibble(uint8_t *byte, int nibble);
void decrement_nibble(uint8_t *byte, int nibble);
