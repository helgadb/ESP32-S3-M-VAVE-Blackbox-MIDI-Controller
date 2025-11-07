#include "midi_storage.h"
#include "globals.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>

static const char *TAG = "MIDI_STORAGE";

void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "NVS initialized");
}

bool load_midi_commands(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open("midi_storage", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved MIDI commands found, using defaults");
        for (int i = 0; i < BUTTON_COUNT; i++) {
            current_commands[i].data[0] = 0x0B;
            current_commands[i].data[1] = 0xB0;
            current_commands[i].data[2] = 0x00;
            current_commands[i].data[3] = 0x00;
            snprintf(current_commands[i].description, sizeof(current_commands[i].description), "Button %d", i + 1);
        }
        return false;
    }

    bool success = true;
    for (int button = 0; button < BUTTON_COUNT; button++) {
        for (int i = 0; i < 4; i++) {
            char key[15];
            snprintf(key, sizeof(key), "btn%d_byte%d", button, i);
            err = nvs_get_u8(nvs_handle, key, &current_commands[button].data[i]);
            if (err != ESP_OK) {
                success = false;
                current_commands[button].data[0] = 0x0B;
                current_commands[button].data[1] = 0xB0;
                current_commands[button].data[2] = 0x00;
                current_commands[button].data[3] = 0x00;
                snprintf(current_commands[button].description, sizeof(current_commands[button].description), "Button %d", button + 1);
                break;
            }
        }
        if (!success) break;
    }

    nvs_close(nvs_handle);

    if (success) {
        ESP_LOGI(TAG, "MIDI commands loaded successfully");
        for (int i = 0; i < BUTTON_COUNT; i++) {
            ESP_LOGI(TAG, "Button %d: %02X %02X %02X %02X", i + 1,
                    current_commands[i].data[0], current_commands[i].data[1],
                    current_commands[i].data[2], current_commands[i].data[3]);
        }
    } else {
        ESP_LOGI(TAG, "Failed to load MIDI commands, using defaults");
    }

    return success;
}

void save_midi_commands(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open("midi_storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }

    for (int button = 0; button < BUTTON_COUNT; button++) {
        for (int i = 0; i < 4; i++) {
            char key[15];
            snprintf(key, sizeof(key), "btn%d_byte%d", button, i);
            err = nvs_set_u8(nvs_handle, key, current_commands[button].data[i]);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error saving button %d byte %d: %s", button, i, esp_err_to_name(err));
                nvs_close(nvs_handle);
                return;
            }
        }
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing to NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "All MIDI commands saved successfully");
    }

    nvs_close(nvs_handle);
}
