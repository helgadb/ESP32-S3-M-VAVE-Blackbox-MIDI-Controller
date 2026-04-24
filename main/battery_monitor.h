#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>

void battery_monitor_init(void);
uint8_t battery_monitor_get_percentage(void);
void battery_monitor_update(void);
void battery_check_and_warn(void);
void battery_show_warning_once(void);
void battery_restore_normal_screen(void); 

#endif