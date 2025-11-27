//midi_storage.h
#pragma once
#include <stdbool.h>
#include "midi_tx_router.h"
void init_nvs(void);
bool load_midi_commands(void);
void save_midi_commands(void);
bool save_usb_mode(usb_operation_mode_t mode);
usb_operation_mode_t load_usb_mode(void);