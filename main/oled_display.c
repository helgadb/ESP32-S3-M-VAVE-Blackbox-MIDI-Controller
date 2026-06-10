#include "oled_display.h"
#include "globals.h"
#include "ssd1306.h"
#include <string.h>

// Função pública para desenhar número grande em standby
void draw_big_number_standby(void)
{
    if (!display_initialized) return;
    
    // Limpar TODO o display
    ssd1306_clear_screen(&dev, false);
    for (int page = 0; page < dev._pages; page++) {
        memset(dev._page[page]._segs, 0, 128);
    }
    
    char num_str[3];
    snprintf(num_str, sizeof(num_str), "%d", current_page);
    int text_len = strlen(num_str);
    
    // Centralizar verticalmente (x6 ocupa 6 páginas)
    int start_page = (dev._pages - 6) / 2;  // (8 - 6) / 2 = 1
    
    // Para centralizar horizontalmente, precisamos calcular espaços
    // Cada caractere ocupa 48 bytes. O display tem 128 bytes.
    // Um dígito: (128 - 48) / 2 = 40 bytes de margem esquerda
    // Dois dígitos: (128 - 96) / 2 = 16 bytes de margem esquerda
    
    // Como a função não aceita margem, vamos usar uma string com espaços
    char spaced_text[10];
    if (text_len == 1) {
        // 1 dígito: adicionar espaços para centralizar
        // Cada espaço ocupa 48 bytes. Precisamos de ~40 bytes de margem
        // 1 espaço = 48 bytes (chega perto)
        spaced_text[0] = ' ';
        spaced_text[1] = num_str[0];
        spaced_text[2] = '\0';
    } else {
        spaced_text[0] = num_str[0];
        spaced_text[1] = num_str[1];
        spaced_text[2] = '\0';
    }
    
    ssd1306_display_text_x6(&dev, start_page, spaced_text, strlen(spaced_text), false);
    
    ssd1306_show_buffer(&dev);
}

void init_oled(void)
{
    i2c_master_init(&dev, I2C_SDA_GPIO, I2C_SCL_GPIO, I2C_RESET_GPIO);
    ssd1306_init(&dev, OLED_WIDTH, OLED_HEIGHT);
    ssd1306_clear_screen(&dev, false);
    ssd1306_contrast(&dev, 0xff);
    display_initialized = true;
    update_display_partial();
}

void update_display_partial(void)
{
    if (!display_on) return;
    if (!display_initialized) return;

    switch (current_mode) {
        case MODE_NORMAL:
        {
            char top_line[17];
            snprintf(top_line, sizeof(top_line), "Bat:%3d%% Pg:%d/7", g_battery_percent, current_page);
            ssd1306_display_text(&dev, 0, top_line, 16, false);
            ssd1306_display_text(&dev, 1, "----------------", 16, false);

            int start_btn = current_page * BUTTONS_PER_PAGE;
            for (int i = 0; i < VISIBLE_BUTTONS; i++) {
                int button_index = scroll_offset + i;
                char button_line[17];
                char *btn_ptr = button_line;

                if (button_index < BUTTONS_PER_PAGE) {
                    int absolute_btn = start_btn + button_index;
                    int physical_btn_num = button_index + 1;
                    
                    *btn_ptr++ = (button_index == current_button) ? '>' : ' ';
                    *btn_ptr++ = 'B';
                    if (physical_btn_num >= 10) {
                        *btn_ptr++ = '1';
                        *btn_ptr++ = '0';
                    } else {
                        *btn_ptr++ = '0' + physical_btn_num;
                        *btn_ptr++ = ' ';
                    }
                    *btn_ptr++ = ':';
                    
                    for (int j = 0; j < 4; j++) {
                        uint8_t byte = current_commands[absolute_btn].data[j];
                        *btn_ptr++ = "0123456789ABCDEF"[byte >> 4];
                        *btn_ptr++ = "0123456789ABCDEF"[byte & 0x0F];
                    }
                    *btn_ptr = '\0';
                    ssd1306_display_text(&dev, 2 + i, button_line, 16, false);
                } else {
                    ssd1306_display_text(&dev, 2 + i, "                ", 16, false);
                }
            }
            ssd1306_display_text(&dev, 7, "*Edit", 5, false);
            break;
        }

        case MODE_EDIT:
        {
            if (!edit_initialized) {
                int absolute_btn = get_absolute_button_index(current_page, current_button);
                int btn_num = absolute_btn + 1;
                
                char title[17];
                int pos = 0;
                title[pos++] = 'V';
                if (btn_num >= 10) {
                    title[pos++] = '0' + (btn_num / 10);
                    title[pos++] = '0' + (btn_num % 10);
                } else {
                    title[pos++] = '0' + btn_num;
                    title[pos++] = ' ';
                }
                title[pos++] = ' ';
                title[pos++] = 'P';
                title[pos++] = 'g';
                title[pos++] = '0' + current_page;
                title[pos++] = '/';
                title[pos++] = '7';
                title[pos] = '\0';
                
                ssd1306_display_text(&dev, 0, title, 16, false);
                ssd1306_display_text(&dev, 1, "----------------", 16, false);
                ssd1306_display_text(&dev, 5, "Up/Dn:Change   ", 16, false);
                ssd1306_display_text(&dev, 6, "*:Next #:Save  ", 16, false);
                ssd1306_display_text(&dev, 7, "Hold#:Cancel   ", 16, false);
                edit_initialized = true;
                ssd1306_display_text(&dev, 2, "                ", 16, false);
                ssd1306_display_text(&dev, 3, "                ", 16, false);
                ssd1306_display_text(&dev, 4, "                ", 16, false);
            }

            char display_line[17];
            char *ptr = display_line;
            for (int byte_idx = 0; byte_idx < 4; byte_idx++) {
                uint8_t byte = edit_command.data[byte_idx];
                char nibble_high = "0123456789ABCDEF"[byte >> 4];
                char nibble_low = "0123456789ABCDEF"[byte & 0x0F];
                if (byte_idx == edit_byte_index) {
                    if (edit_nibble_index == 0) {
                        *ptr++ = '['; *ptr++ = nibble_high; *ptr++ = ']'; *ptr++ = nibble_low;
                    } else {
                        *ptr++ = nibble_high; *ptr++ = '['; *ptr++ = nibble_low; *ptr++ = ']';
                    }
                } else {
                    *ptr++ = nibble_high; *ptr++ = nibble_low;
                }
            }
            *ptr = '\0';
            ssd1306_display_text(&dev, 3, display_line, 16, false);
            ssd1306_display_text(&dev, 4, "                ", 16, false);
            break;
        }
    }
    
    ssd1306_show_buffer(&dev);
}