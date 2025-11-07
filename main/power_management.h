#pragma once
#include <stdbool.h>

void init_power_management(void);
void power_management_task(void *arg);
void set_cpu_power_save_mode(void);
void set_cpu_full_performance_mode(void);
void display_power_save(bool enable);
void update_cpu_activity_time(void);
void update_display_activity_time(void);
