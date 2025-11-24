/*
 * MIDI UART helper library
 * Provides UART init, send (used by USB->UART forwarding), and a parser helper
 * to convert UART raw stream into USB MIDI 4-byte packets and forward them via midi_send_data().
 *
 * Implements a queue-based low-latency forwarder for USB->UART:
 *  - driver enqueues received USB packets (non-blocking)
 *  - high-priority task drains the queue and writes to UART immediately
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "midi_uart.h"
#include "midi_class_driver_txrx.h" // for midi_send_data and midi_driver_ready_for_tx

static const char *TAG = "MIDI_UART";

// Default UART configuration - can be edited here if needed
#define UART_NUM               UART_NUM_1
#define UART_BAUD_RATE         31250
#define UART_RX_PIN            4      // GPIO4 - MIDI IN (top)
#define UART_TX_PIN            5      // GPIO5 - MIDI OUT (top)
#define UART_BUFFER_SIZE       2048

// USB->UART queue: each item holds up to 256 bytes of raw USB MIDI stream (+2 length bytes)
static QueueHandle_t usb_uart_queue = NULL;
#define USB_UART_QUEUE_LEN    256
#define USB_UART_ITEM_SIZE    260 // 2 bytes length + up to 258 payload

void midi_uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    // Instalar driver UART
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_BUFFER_SIZE, UART_BUFFER_SIZE, 10, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    ESP_LOGI(TAG, "UART MIDI initialized (baud=%d, RX=GPIO%d, TX=GPIO%d)", UART_BAUD_RATE, UART_RX_PIN, UART_TX_PIN);
}

// This function is intended to be called by the USB driver (via queue or fallback).
void midi_uart_send_to_uart(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0) {
        ESP_LOGE(TAG, "midi_uart_send_to_uart: invalid args");
        return;
    }

    // Send directly to UART
    int written = uart_write_bytes(UART_NUM, (const char*)data, length);
    if (written != (int)length) {
        ESP_LOGW(TAG, "Direct UART write mismatch: expected %d wrote %d", (int)length, written);
    }
    // ensure TX finishes quickly
    uart_wait_tx_done(UART_NUM, pdMS_TO_TICKS(20));
}

/*
 * Parse UART raw buffer and convert to USB MIDI packets (4 bytes each),
 * then call midi_send_data() for each packet.
 *
 * This helper implements a simple parser that assumes status bytes are present.
 */
void midi_uart_parse_and_send_to_usb(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0) return;

    int pos = 0;
    while (pos < (int)length) {
        uint8_t status_byte = data[pos];
        
        if (status_byte >= 0x80) {
            uint8_t message_type = status_byte & 0xF0;
            uint8_t cable_number = 0; // Cable 0 for standard MIDI
            uint8_t cin = 0;
            uint8_t usb_packet[4] = {0};
            
            switch (message_type) {
                case 0x80: // Note Off
                case 0x90: // Note On
                case 0xA0: // Polyphonic Key Pressure
                case 0xB0: // Control Change
                case 0xE0: // Pitch Bend
                    cin = message_type >> 4;
                    if (pos + 2 < (int)length) {
                        usb_packet[0] = (cable_number << 4) | cin;
                        usb_packet[1] = data[pos];
                        usb_packet[2] = data[pos + 1];
                        usb_packet[3] = data[pos + 2];
                        
                        if (midi_driver_ready_for_tx()) {
                            midi_send_data(usb_packet, 4);
                        }
                        pos += 3;
                    } else {
                        pos = length;
                    }
                    break;
                    
                case 0xC0: // Program Change (2 bytes)
                case 0xD0: // Channel Pressure (2 bytes)
                    cin = message_type >> 4;
                    if (pos + 1 < (int)length) {
                        usb_packet[0] = (cable_number << 4) | cin;
                        usb_packet[1] = data[pos];
                        usb_packet[2] = data[pos + 1];
                        usb_packet[3] = 0x00;
                        
                        if (midi_driver_ready_for_tx()) {
                            midi_send_data(usb_packet, 4);
                        }
                        pos += 2;
                    } else {
                        pos = length;
                    }
                    break;
                    
                case 0xF0: // System Messages
                    if (status_byte == 0xF0) { // SysEx Start
                        usb_packet[0] = (cable_number << 4) | 0x04; // SysEx starts/continues
                        usb_packet[1] = data[pos];
                        usb_packet[2] = 0x00;
                        usb_packet[3] = 0x00;
                        if (midi_driver_ready_for_tx()) {
                            midi_send_data(usb_packet, 4);
                        }
                        pos++;
                    } else {
                        usb_packet[0] = (cable_number << 4) | 0x05;
                        usb_packet[1] = data[pos];
                        usb_packet[2] = 0x00;
                        usb_packet[3] = 0x00;
                        if (midi_driver_ready_for_tx()) {
                            midi_send_data(usb_packet, 4);
                        }
                        pos++;
                    }
                    break;
                    
                default:
                    pos++; // unknown, skip
                    break;
            }
        } else {
            pos++;
        }
    }
}

// Internal task that drains usb_uart_queue and writes to UART with minimal latency.
static void midi_uart_usb_to_uart_task(void *arg)
{
    uint8_t item[USB_UART_ITEM_SIZE];
    while (1) {
        if (usb_uart_queue == NULL) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (xQueueReceive(usb_uart_queue, item, portMAX_DELAY) == pdTRUE) {
            uint16_t len = (uint16_t)((item[0] << 8) | item[1]);
            if (len > 0 && len <= (USB_UART_ITEM_SIZE - 2)) {
                // Send directly to UART
                int written = uart_write_bytes(UART_NUM, (const char*)(item + 2), len);
                if (written != (int)len) {
                    ESP_LOGW(TAG, "usb_to_uart task: wrote %d/%d bytes", written, (int)len);
                }
                // Wait briefly for TX to complete to maintain timing
                uart_wait_tx_done(UART_NUM, pdMS_TO_TICKS(20));
            }
        }
    }
}

// Non-blocking try to enqueue USB data for forwarding.
// Returns true on success, false on failure (e.g., queue full or not initialized).
bool midi_uart_try_enqueue_usb(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0 || usb_uart_queue == NULL) return false;
    if (length > USB_UART_ITEM_SIZE - 2) {
        // too big to queue
        return false;
    }
    uint8_t item[USB_UART_ITEM_SIZE];
    item[0] = (uint8_t)((length >> 8) & 0xFF);
    item[1] = (uint8_t)(length & 0xFF);
    memcpy(item + 2, data, length);
    BaseType_t res = xQueueSend(usb_uart_queue, item, 0); // non-blocking
    return (res == pdTRUE);
}

// Start the usb_to_uart task and create the queue.
// priority: FreeRTOS priority for the task
// stack_size: stack size in bytes
// core: core id to pin the task (use 0 or 1)
void midi_uart_start_usb_to_uart_task(UBaseType_t priority, uint32_t stack_size, BaseType_t core)
{
    if (usb_uart_queue == NULL) {
        usb_uart_queue = xQueueCreate(USB_UART_QUEUE_LEN, USB_UART_ITEM_SIZE);
        if (usb_uart_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create usb_uart_queue");
            return;
        }
    }
    BaseType_t ok = xTaskCreatePinnedToCore(midi_uart_usb_to_uart_task, "usb_to_uart_q", stack_size, NULL, priority, NULL, core);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create usb_to_uart_q task");
    } else {
        ESP_LOGI(TAG, "usb_to_uart_q task started (priority=%d, core=%d)", (int)priority, (int)core);
    }
}
