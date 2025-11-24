#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize UART for MIDI (configurable pins via defines or default)
void midi_uart_init(void);

// Send raw bytes to UART MIDI OUT
void midi_uart_send_to_uart(const uint8_t *data, size_t length);

// Parse raw UART buffer and forward to USB (uses midi_send_data internally)
void midi_uart_parse_and_send_to_usb(const uint8_t *data, size_t length);

// Try to enqueue raw USB MIDI packet bytes for low-latency forwarding to UART.
// Non-blocking; returns true if queued, false if queue full or not initialized.
bool midi_uart_try_enqueue_usb(const uint8_t *data, size_t length);

// Start the internal USB->UART forwarding task which drains the queue and writes to UART.
// Call from main to create the task with appropriate priority.
void midi_uart_start_usb_to_uart_task(UBaseType_t priority, uint32_t stack_size, BaseType_t core);

#ifdef __cplusplus
}
#endif
