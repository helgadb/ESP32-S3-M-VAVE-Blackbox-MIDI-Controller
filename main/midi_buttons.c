#include "midi_buttons.h"
#include "globals.h"
#include "midi_class_driver_txrx.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_management.h"
#include "midi_tx_router.h"

void init_midi_buttons(void)
{
    uint64_t button_mask = 0;
    for (int i = 0; i < BUTTONS_PER_PAGE; i++) {
        button_mask |= (1ULL << button_gpios[i]);
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = button_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    init_button_states();
}

void button_check_task(void *arg)
{
    init_midi_buttons();

    const uint32_t DEBOUNCE_DELAY_MS = 50;      // 50ms de debounce
    const uint32_t LONG_PRESS_MS_WITH_DEBOUNCE = LONG_PRESS_MS + DEBOUNCE_DELAY_MS;
    
    // Variável de estado local para rastrear se o afinador está ativo
    static bool in_tuner_mode = false;
    
    while (1) {
        uint32_t current_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        for (int i = 0; i < BUTTONS_PER_PAGE; i++) {
            bool current_state = gpio_get_level(button_gpios[i]);
            
            // ===============================================
            // DETECTAR BORDA DE DESCIDA (botão pressionado)
            // ===============================================
            if (last_button_states[i] && !current_state) {
                if ((current_time_ms - button_press_start_ms[i]) >= DEBOUNCE_DELAY_MS) {
                    button_press_start_ms[i] = current_time_ms;
                    long_press_triggered[i] = false;
                    
                    update_cpu_activity_time();
                    if (cpu_power_save_mode) {
                        set_cpu_full_performance_mode();
                    }
                }
            }
            
            // ===============================================
            // DETECTAR BORDA DE SUBIDA (botão liberado - Toque Curto)
            // ===============================================
            if (!last_button_states[i] && current_state) {
                uint32_t press_duration = current_time_ms - button_press_start_ms[i];
                uint32_t actual_duration = (press_duration > DEBOUNCE_DELAY_MS) ? 
                                           (press_duration - DEBOUNCE_DELAY_MS) : 0;
                
                if (actual_duration < LONG_PRESS_MS && !long_press_triggered[i]) {
                    extern void battery_restore_normal_screen(void);
                    battery_restore_normal_screen();
                    
                    if (current_mode == MODE_NORMAL) {
                        if (current_button != i) {
                            current_button = i;
                            if (i < scroll_offset) {
                                scroll_offset = i;
                            } else if (i >= scroll_offset + VISIBLE_BUTTONS) {
                                scroll_offset = i - VISIBLE_BUTTONS + 1;
                            }
                        }
                        if (display_on) {
                            update_display_partial();
                        }
                    }
                    
                    // Enviar comando MIDI ou Navegar (Toque Curto)
                    if (current_page < 8) {
                        // Páginas de Presets (0 a 7): envia comando MIDI do Preset
                        int absolute_index = get_absolute_button_index(current_page, i);
                        midi_tx_router_send(current_commands[absolute_index].data, 
                                            sizeof(current_commands[absolute_index].data));
                    } else {
                        // Páginas de Effect Change (8 a 87)
                        int preset_abs_idx = current_page - 8;
                        int absolute_index = get_absolute_button_index(current_page, i);
                        
                        if (i == 8) { 
                            // Botão 9 (Toque curto): Descer para o preset imediatamente anterior
                            if (preset_abs_idx > 0) {
                                int target_preset_idx = preset_abs_idx - 1;
                                set_page(8 + target_preset_idx);
                                midi_tx_router_send(current_commands[target_preset_idx].data, 
                                                    sizeof(current_commands[target_preset_idx].data));
                            }
                        } else if (i == 9) { 
                            // Botão 10 (Toque curto): Subir para o preset imediatamente posterior
                            if (preset_abs_idx < 79) {
                                int target_preset_idx = preset_abs_idx + 1;
                                set_page(8 + target_preset_idx);
                                midi_tx_router_send(current_commands[target_preset_idx].data, 
                                                    sizeof(current_commands[target_preset_idx].data));
                            }
                        } else {
                            // Botões 1 a 8 (inclui Botão 4 = Tap MOD e Botão 5 = Tap DLY):
                            // envia o comando MIDI cadastrado na NVS
                            midi_tx_router_send(current_commands[absolute_index].data, 
                                                sizeof(current_commands[absolute_index].data));
                        }
                    }
                }
                
                long_press_triggered[i] = false;
                button_press_start_ms[i] = 0;
            }
            
            // ===============================================
            // VERIFICAR PRESSIONAMENTO LONGO (> 500ms)
            // ===============================================
            if (!last_button_states[i] && !current_state && !long_press_triggered[i]) {
                uint32_t press_duration = current_time_ms - button_press_start_ms[i];
                
                if (press_duration >= LONG_PRESS_MS_WITH_DEBOUNCE) {
                    long_press_triggered[i] = true;
                    last_cpu_activity_time = xTaskGetTickCount();
                    
                    if (current_mode == MODE_NORMAL) {
                        if (i == 4) {
                            // Botão 5 (Toque longo): Alterna estado do AFINADOR (Ligar/Desligar)
                            if (!in_tuner_mode) {
                                uint8_t tuner_on[4] = {0x0B, 0xB0, 0x1C, 0x7F}; // CC 28, Valor 127 (Ligar)
                                midi_tx_router_send(tuner_on, sizeof(tuner_on));
                                in_tuner_mode = true;
                            } else {
                                uint8_t tuner_off[4] = {0x0B, 0xB0, 0x1C, 0x00}; // CC 28, Valor 0 (Desligar)
                                midi_tx_router_send(tuner_off, sizeof(tuner_off));
                                in_tuner_mode = false;
                            }
                        } else {
                            // Demais botões em Long Press (Navegação unificada para todas as páginas)
                            if (current_page < 8) {
                                // Mapeamento de Long Press nas Páginas de Presets (0 a 7)
                                int target_page = -1;
                                switch (i) {
                                    case 0: target_page = 0; break; // Botão 1 -> Página 0
                                    case 1: target_page = 1; break; // Botão 2 -> Página 1
                                    case 2: target_page = 2; break; // Botão 3 -> Página 2
                                    case 3: target_page = 3; break; // Botão 4 -> Página 3
                                    case 5: target_page = 4; break; // Botão 6 -> Página 4
                                    case 6: target_page = 5; break; // Botão 7 -> Página 5
                                    case 7: target_page = 6; break; // Botão 8 -> Página 6
                                    case 8: target_page = 7; break; // Botão 9 -> Página 7
                                    case 9: {
                                        // Botão 10 (Long Press): Entra no Effect Change do preset atual
                                        int preset_abs_idx = current_page * BUTTONS_PER_PAGE + current_button;
                                        set_page(8 + preset_abs_idx);
                                        break;
                                    }
                                    default: break;
                                }
                                
                                if (target_page != -1) {
                                    set_page(target_page);
                                }
                            } else {
                                // Mapeamento de Long Press nas Páginas de Effect Change (8 a 87)
                                if (i == 9) {
                                    // Botão 10 (Long Press): SAIR (volta para a página de presets correspondente)
                                    int preset_abs_idx = current_page - 8;
                                    int target_preset_page = preset_abs_idx / 10;
                                    int target_button = preset_abs_idx % 10;
                                    set_page_and_button(target_preset_page, target_button);
                                } else {
                                    // Para os botões 1 a 4 e 6 a 9 na tela de efeito, 
                                    // você pode mapear páginas por long press se desejar o comportamento igual:
                                    int target_page = -1;
                                    switch (i) {
                                        case 0: target_page = 0; break;
                                        case 1: target_page = 1; break;
                                        case 2: target_page = 2; break;
                                        case 3: target_page = 3; break;
                                        case 5: target_page = 4; break;
                                        case 6: target_page = 5; break;
                                        case 7: target_page = 6; break;
                                        case 8: target_page = 7; break;
                                        default: break;
                                    }
                                    if (target_page != -1) {
                                        set_page(target_page);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            last_button_states[i] = current_state;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}