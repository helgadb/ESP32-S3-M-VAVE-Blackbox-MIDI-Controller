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
    
    while (1) {
        uint32_t current_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        for (int i = 0; i < BUTTONS_PER_PAGE; i++) {
            bool current_state = gpio_get_level(button_gpios[i]);
            
            // ===============================================
            // DETECTAR BORDA DE DESCIDA (botão pressionado)
            // ===============================================
            if (last_button_states[i] && !current_state) {
                // Verificar debounce baseado em timestamp
                if ((current_time_ms - button_press_start_ms[i]) >= DEBOUNCE_DELAY_MS) {
                    button_press_start_ms[i] = current_time_ms;
                    long_press_triggered[i] = false;
                    
                    // Atualizar timers de atividade
                    update_cpu_activity_time();
                    if (cpu_power_save_mode) {
                        set_cpu_full_performance_mode();
                    }
                }
            }
            
            // ===============================================
            // DETECTAR BORDA DE SUBIDA (botão liberado)
            // ===============================================
            if (!last_button_states[i] && current_state) {
                uint32_t press_duration = current_time_ms - button_press_start_ms[i];
                
                // Subtrair o debounce da duração para obter o tempo real
                uint32_t actual_duration = (press_duration > DEBOUNCE_DELAY_MS) ? 
                                           (press_duration - DEBOUNCE_DELAY_MS) : 0;
                
// Pressionamento curto (menos que LONG_PRESS_MS e não foi long press)
                if (actual_duration < LONG_PRESS_MS && !long_press_triggered[i]) {
                    extern void battery_restore_normal_screen(void);
                    battery_restore_normal_screen();
                    
                    // SEMPRE atualiza a seleção do botão no modo normal (mesmo se estivesse em standby)
                    if (current_mode == MODE_NORMAL) {
                        if (current_button != i) {
                            current_button = i;
                            if (i < scroll_offset) {
                                scroll_offset = i;
                            } else if (i >= scroll_offset + VISIBLE_BUTTONS) {
                                scroll_offset = i - VISIBLE_BUTTONS + 1;
                            }
                        }
                        // Atualiza o display apenas se ele estiver ligado
                        if (display_on) {
                            update_display_partial();
                        }
                    }
                    
                    // Enviar comando MIDI
                    if (current_page < 8) {
                        // Enviar comando MIDI do Preset
                        int absolute_index = get_absolute_button_index(current_page, i);
                        midi_tx_router_send(current_commands[absolute_index].data, 
                                            sizeof(current_commands[absolute_index].data));
                    } else {
                        // Páginas de Effect Change
                        int preset_abs_idx = current_page - 8;
                        int absolute_index = get_absolute_button_index(current_page, i);
                        
                        if (i == 3) { 
                            // Botão 4: Descer para o efeito do preset imediatamente anterior
                            if (preset_abs_idx > 0) {
                                int target_preset_idx = preset_abs_idx - 1;
                                set_page(8 + target_preset_idx);
                                // Envia o comando MIDI do preset anterior para atualizar a pedaleira
                                midi_tx_router_send(current_commands[target_preset_idx].data, 
                                                    sizeof(current_commands[target_preset_idx].data));
                            }
                        } else if (i == 4) { 
                            // Botão 5: Subir para o efeito do preset imediatamente posterior
                            if (preset_abs_idx < 79) {
                                int target_preset_idx = preset_abs_idx + 1;
                                set_page(8 + target_preset_idx);
                                // Envia o comando MIDI do preset posterior para atualizar a pedaleira
                                midi_tx_router_send(current_commands[target_preset_idx].data, 
                                                    sizeof(current_commands[target_preset_idx].data));
                            }
                        } else if (i == 9) { 
                            // Botão 10: SAIR (volta para a página de presets correspondente e seleciona o botão exato)
                            int target_preset_page = preset_abs_idx / 10;
                            int target_button = preset_abs_idx % 10;
                            set_page_and_button(target_preset_page, target_button);
                        } else {
                            // Demais botões (1, 2, 3, 6, 7, 8, 9): enviam comando MIDI e customizáveis
                            midi_tx_router_send(current_commands[absolute_index].data, 
                                                sizeof(current_commands[absolute_index].data));
                        }
                    }
                }
                
                // Resetar flags
                long_press_triggered[i] = false;
                button_press_start_ms[i] = 0;
            }
            
            // ===============================================
            // VERIFICAR PRESSIONAMENTO LONGO (enquanto pressionado)
            // ===============================================
            if (!last_button_states[i] && !current_state && !long_press_triggered[i]) {
                uint32_t press_duration = current_time_ms - button_press_start_ms[i];
                
                // Verificar se atingiu o tempo de long press (considerando debounce)
                if (press_duration >= LONG_PRESS_MS_WITH_DEBOUNCE) {
                    long_press_triggered[i] = true;
                    
                    // Atualizar apenas timer da CPU, NÃO o timer do display
                    last_cpu_activity_time = xTaskGetTickCount();
                    
                    // Mudar página por mapeamento direto de long press
                    if (current_mode == MODE_NORMAL) {
                        int target_page = -1;
                        switch (i) {
                            case 0: target_page = 0; break; // Botão 1 -> Página 0
                            case 1: target_page = 1; break; // Botão 2 -> Página 1
                            case 2: target_page = 2; break; // Botão 3 -> Página 2
                            case 3: target_page = 3; break; // Botão 4 -> Página 3
                            // case 4 (Botão 5): sem função por enquanto
                            case 5: target_page = 4; break; // Botão 6 -> Página 4
                            case 6: target_page = 5; break; // Botão 7 -> Página 5
                            case 7: target_page = 6; break; // Botão 8 -> Página 6
                            case 8: target_page = 7; break; // Botão 9 -> Página 7
                            case 9: {
                                    // Botão 10 (long press): Entra na página de Effect Change do botão atual
                                    int preset_abs_idx = current_page * BUTTONS_PER_PAGE + current_button;
                                    set_page(8 + preset_abs_idx);
                                    break;
                                }
                            default: break;
                        }
                        
                        if (target_page != -1) {
                            set_page(target_page);
                        }
                    }
                }
            }
            
            last_button_states[i] = current_state;
        }
        
        // Pequeno delay não bloqueante - permite que outras tasks rodem
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}