//midi_device_tx.h
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

bool midi_device_send(const uint8_t *data, size_t length);
