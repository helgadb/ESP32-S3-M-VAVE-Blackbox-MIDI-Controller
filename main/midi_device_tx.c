//midi_device_tx.c
#include "midi_device_tx.h"
#include "tinyusb.h"
#include "esp_log.h"

static const char *TAG = "MIDI_DEVICE_TX";

bool midi_device_send(const uint8_t *data, size_t length)
{
    if (!data || length == 0) {
        ESP_LOGW(TAG, "Empty MIDI message ignored");
        return false;
    }

    if (!tud_midi_mounted()) {
        ESP_LOGW(TAG, "tud_midi_mounted() = false");
        return false;
    }

    // Envia pacote MIDI USB
    uint32_t written = tud_midi_stream_write(0, data, length);

    if (written != length) {
        ESP_LOGW(TAG, "TinyUSB wrote only %lu of %u bytes", written, length);
        return false;
    }

    ESP_LOGI(TAG, "DEVICE MIDI TX OK (%u bytes): %02X %02X %02X",
             length,
             data[0],
             (length > 1 ? data[1] : 0),
             (length > 2 ? data[2] : 0));

    return true;
}
