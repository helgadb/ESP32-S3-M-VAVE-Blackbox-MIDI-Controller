#include "oled_display.h"
#include "globals.h"
#include "ssd1306.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "OLED";

void init_oled(void)
{
    ESP_LOGI(TAG, "Initializing OLED with I2C NG Driver...");
    ESP_LOGI(TAG, "SDA_GPIO=%d, SCL_GPIO=%d, RESET_GPIO=%d",
             I2C_SDA_GPIO, I2C_SCL_GPIO, I2C_RESET_GPIO);

    i2c_master_init(&dev, I2C_SDA_GPIO, I2C_SCL_GPIO, I2C_RESET_GPIO);
    ssd1306_init(&dev, OLED_WIDTH, OLED_HEIGHT);
    ssd1306_clear_screen(&dev, false);
    ssd1306_contrast(&dev, 0xff);

    display_initialized = true;
    ESP_LOGI(TAG, "OLED initialized successfully");

    update_display_partial();
}

// Função auxiliar para desenhar o ícone/porcentagem da bateria
static void draw_battery_status(void) {
    char battery_str[16];
    // Mostra a bateria no canto superior direito (posição 16, pois linha tem 21 caracteres)
    // Ajuste a posição conforme necessário - 16 alinha à direita
    snprintf(battery_str, sizeof(battery_str), "Battery:%3d%%", g_battery_percent);
    ssd1306_display_text(&dev, 0, battery_str, strlen(battery_str), false);
}

void update_display_partial(void)
{
    switch (current_mode) {
        case MODE_NORMAL:
            // Linha 0: Título e bateria lado a lado
            //ssd1306_display_text(&dev, 0, "BUTTON CONFIG", 12, false);
            draw_battery_status();  // Mostra bateria no canto direito
            
            ssd1306_display_text(&dev, 1, "----------------", 16, false);

            for (int i = 0; i < VISIBLE_BUTTONS; i++) {
                int button_index = scroll_offset + i;
                char button_line[18];
                char *btn_ptr = button_line;

                if (button_index < BUTTON_COUNT) {
                    *btn_ptr++ = (button_index == current_button) ? '>' : ' ';
                    *btn_ptr++ = 'B';
                    int btn_num = button_index + 1;
                    if (btn_num >= 10) {
                        *btn_ptr++ = '1';
                        *btn_ptr++ = '0' + (btn_num - 10);
                    } else {
                        *btn_ptr++ = '0' + btn_num;
                    }
                    *btn_ptr++ = ':';
                    for (int j = 0; j < 4; j++) {
                        uint8_t byte = current_commands[button_index].data[j];
                        *btn_ptr++ = "0123456789ABCDEF"[byte >> 4];
                        *btn_ptr++ = "0123456789ABCDEF"[byte & 0x0F];
                    }
                    *btn_ptr = '\0';

                    ssd1306_display_text(&dev, 2 + i, button_line, strlen(button_line), false);
                } else {
                    ssd1306_display_text(&dev, 2 + i, "                ", 16, false);
                }
            }
            ssd1306_display_text(&dev, 7, "*:Edit          ", 16, false);
            break;

        case MODE_EDIT:
            if (!edit_initialized) {
                char title[16] = "                ";
                int btn_num = current_button + 1;

                if (btn_num >= 10) {
                    memcpy(title, "Edit BT 10     ", 16);
                    title[8] = '1';
                    title[9] = '0' + (btn_num - 10);
                } else {
                    memcpy(title, "Edit BT 1      ", 16);
                    title[7] = ' ';
                    title[8] = '0' + btn_num;
                }
                ssd1306_display_text(&dev, 0, title, strlen(title), false);
                draw_battery_status();  // Mostra bateria também no modo edição
                
                ssd1306_display_text(&dev, 1, "----------------", 16, false);

                ssd1306_display_text(&dev, 5, "Up/Dn:Change    ", 16, false);
                ssd1306_display_text(&dev, 6, "*:Next #:Save   ", 16, false);
                ssd1306_display_text(&dev, 7, "Hold#:Cancel    ", 16, false);

                edit_initialized = true;
                ssd1306_display_text(&dev, 2, "                ", 16, false);
                ssd1306_display_text(&dev, 3, "                ", 16, false);
                ssd1306_display_text(&dev, 4, "                ", 16, false);
            }

            char display_line[20];
            char *ptr = display_line;

            for (int byte_idx = 0; byte_idx < 4; byte_idx++) {
                uint8_t byte = edit_command.data[byte_idx];
                char nibble_high = "0123456789ABCDEF"[byte >> 4];
                char nibble_low = "0123456789ABCDEF"[byte & 0x0F];

                if (byte_idx == edit_byte_index) {
                    if (edit_nibble_index == 0) {
                        *ptr++ = '[';
                        *ptr++ = nibble_high;
                        *ptr++ = ']';
                        *ptr++ = nibble_low;
                    } else {
                        *ptr++ = nibble_high;
                        *ptr++ = '[';
                        *ptr++ = nibble_low;
                        *ptr++ = ']';
                    }
                } else {
                    *ptr++ = nibble_high;
                    *ptr++ = nibble_low;
                }
            }
            *ptr = '\0';

            ssd1306_display_text(&dev, 3, display_line, strlen(display_line), false);
            ssd1306_display_text(&dev, 4, "                ", 16, false);
            
            // Redesenha a bateria no modo edição (caso tenha sido sobrescrita)
            draw_battery_status();
            break;
    }
}