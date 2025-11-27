//midi_tx_router.c
#include "midi_tx_router.h"
#include "midi_class_driver_txrx.h"
#include "midi_device_tx.h"
#include "esp_log.h"
#include "tusb.h"

static const char *TAG = "MIDI_ROUTER";

// DEFINIÇÃO REAL — gerará o símbolo para o linker
usb_operation_mode_t current_usb_mode = USB_MODE_HOST;

bool midi_tx_router_send(const uint8_t *data, size_t length)
{
    if (!data || length == 0) {
        ESP_LOGW(TAG, "Ignored empty MIDI message");
        return false;
    }

    // -----------------------------------------------------
    // HOST MODE
    // -----------------------------------------------------
    if (current_usb_mode == USB_MODE_HOST)
    {
        if (!midi_driver_ready_for_tx()) {
            ESP_LOGW(TAG, "HOST: USB Host MIDI driver not ready");
            return false;
        }

        bool ok = midi_send_data(data, length);

        ESP_LOGI(TAG, "HOST SEND = %s | %02X %02X %02X",
                 ok ? "OK" : "FAIL",
                 data[0],
                 (length > 1 ? data[1] : 0),
                 (length > 2 ? data[2] : 0));

        return ok;
    }

    // -----------------------------------------------------
    // DEVICE MODE
    // -----------------------------------------------------
    else if (current_usb_mode == USB_MODE_DEVICE)
    {
        bool ok = midi_device_send(data, length);

        ESP_LOGI(TAG, "DEVICE SEND = %s | %02X %02X %02X",
                 ok ? "OK" : "FAIL",
                 data[0],
                 (length > 1 ? data[1] : 0),
                 (length > 2 ? data[2] : 0));

        return ok;
    }

    // -----------------------------------------------------
    // INVALID MODE
    // -----------------------------------------------------
    ESP_LOGE(TAG, "Invalid USB mode: %d", current_usb_mode);
    return false;
}
