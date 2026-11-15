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

static void set_default_command(int btn, uint8_t *data) {
    if (btn < 80) {
        // Páginas de Presets (0 a 79)
        data[0] = 0x0C;
        data[1] = 0xC0;
        data[2] = (uint8_t)btn;
        data[3] = 0x00;
    } else {
        // Páginas de Effect Change (80 a 879)
        int eff_btn_in_page = btn % 10;
        data[3] = 0x00;
        switch (eff_btn_in_page) {
            case 0: // Botão 1: CC0 0 (FX)
                data[0] = 0x0B; data[1] = 0xB0; data[2] = 0x00; data[3] = 0x00; break;
            case 1: // Botão 2: CC2 0 (MOD)
                data[0] = 0x0B; data[1] = 0xB0; data[2] = 0x02; data[3] = 0x00; break;
            case 2: // Botão 3: CC3 0 (DLY)
                data[0] = 0x0B; data[1] = 0xB0; data[2] = 0x03; data[3] = 0x00; break;
            case 3: // Botão 4: TAP TEMPO MOD (Canal 1, CC 40, Valor 127 -> 0x0B, 0xB0, 0x28, 0x7F)
                data[0] = 0x0B; data[1] = 0xB0; data[2] = 0x28; data[3] = 0x7F; break;
            case 4: // Botão 5: TAP TEMPO DLY (Canal 1, CC 41, Valor 127 -> 0x0B, 0xB0, 0x29, 0x7F)
                data[0] = 0x0B; data[1] = 0xB0; data[2] = 0x29; data[3] = 0x7F; break;
            case 5: // Botão 6: CC4 0 (REV)
                data[0] = 0x0B; data[1] = 0xB0; data[2] = 0x04; data[3] = 0x00; break;
            case 6: // Botão 7: CC1 0 (AMP)
                data[0] = 0x0B; data[1] = 0xB0; data[2] = 0x01; data[3] = 0x00; break;
            case 7: // Botão 8: CC5 0 (CAB)
                data[0] = 0x0B; data[1] = 0xB0; data[2] = 0x05; data[3] = 0x00; break;
            case 8: // Botão 9: Preset Anterior (Navegação interna)
                data[0] = 0x00; data[1] = 0x00; data[2] = 0x00; data[3] = 0x00; break;
            case 9: // Botão 10: Preset Posterior (Navegação interna)
                data[0] = 0x00; data[1] = 0x00; data[2] = 0x00; data[3] = 0x00; break;
            default:
                data[0] = 0x00; data[1] = 0x00; data[2] = 0x00; data[3] = 0x00; break;
        }
    }
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