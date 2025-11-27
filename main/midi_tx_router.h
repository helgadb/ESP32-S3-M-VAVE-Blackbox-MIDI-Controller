//midi_tx_router.h
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Modo de operação USB
typedef enum {
    USB_MODE_HOST = 0,
    USB_MODE_DEVICE = 1
} usb_operation_mode_t;

// Definida em main.c
extern usb_operation_mode_t current_usb_mode;

// Função única para enviar MIDI em HOST ou DEVICE
bool midi_tx_router_send(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif