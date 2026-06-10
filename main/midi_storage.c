#include "midi_storage.h"
#include "globals.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>

void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static void set_default_command(int button_index, uint8_t *data) {
    data[0] = 0x0C;
    data[1] = 0xC0;
    data[2] = (uint8_t)button_index;
    data[3] = 0x00;
}

bool load_midi_commands(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open("midi_storage", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        for (int btn = 0; btn < TOTAL_BUTTONS; btn++) {
            set_default_command(btn, current_commands[btn].data);
            snprintf(current_commands[btn].description, sizeof(current_commands[btn].description), 
                     "V%d", btn + 1);
        }
        return false;
    }

    for (int btn = 0; btn < TOTAL_BUTTONS; btn++) {
        bool btn_valid = true;
        for (int i = 0; i < 4; i++) {
            char key[20];
            snprintf(key, sizeof(key), "btn%d_byte%d", btn, i);
            err = nvs_get_u8(nvs_handle, key, &current_commands[btn].data[i]);
            if (err != ESP_OK) {
                btn_valid = false;
                break;
            }
        }
        
        if (!btn_valid) {
            for (int b = 0; b < TOTAL_BUTTONS; b++) {
                set_default_command(b, current_commands[b].data);
                snprintf(current_commands[b].description, sizeof(current_commands[b].description), 
                         "V%d", b + 1);
            }
            nvs_close(nvs_handle);
            return false;
        }
        
        snprintf(current_commands[btn].description, sizeof(current_commands[btn].description), 
                 "V%d", btn + 1);
    }

    nvs_close(nvs_handle);
    return true;
}

void save_midi_commands(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open("midi_storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return;
    }

    for (int btn = 0; btn < TOTAL_BUTTONS; btn++) {
        for (int i = 0; i < 4; i++) {
            char key[20];
            snprintf(key, sizeof(key), "btn%d_byte%d", btn, i);
            err = nvs_set_u8(nvs_handle, key, current_commands[btn].data[i]);
            if (err != ESP_OK) {
                nvs_close(nvs_handle);
                return;
            }
        }
    }

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
}

bool save_usb_mode(usb_operation_mode_t mode)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("midi_storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_set_u8(nvs_handle, "usb_mode", (uint8_t)mode);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return err == ESP_OK;
}

usb_operation_mode_t load_usb_mode(void)
{
    nvs_handle_t nvs_handle;
    uint8_t mode_u8 = USB_MODE_HOST;

    esp_err_t err = nvs_open("midi_storage", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return USB_MODE_HOST;
    }

    err = nvs_get_u8(nvs_handle, "usb_mode", &mode_u8);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        return (usb_operation_mode_t)mode_u8;
    }

    return USB_MODE_HOST;
}