#include "globals.h"
#include "midi_storage.h"
#include "oled_display.h"
#include "navigation.h"
#include "midi_buttons.h"
#include "power_management.h"
#include "usb_daemon.h"
#include "midi_class_driver_txrx.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

#define DAEMON_TASK_PRIORITY     2
#define CLASS_TASK_PRIORITY      3
#define BUTTON_TASK_PRIORITY     3
#define NAVIGATION_TASK_PRIORITY 3
#define POWER_TASK_PRIORITY      1

//====================================================
//   USB → UART fallback desativado neste dispositivo
//====================================================
void process_usb_rx_for_uart(const uint8_t *data, size_t length)
{
    // Este equipamento NÃO possui UART
    // Mantemos a função para compatibilidade com o driver
    (void)data;
    (void)length;
}

//====================================================
//                APP MAIN
//====================================================
void app_main(void)
{
    ESP_LOGI(TAG, "Starting system (USB-only mode)");

    // Inicializações originais do projeto
    init_power_management();
    last_cpu_activity_time = xTaskGetTickCount();
    last_display_activity_time = xTaskGetTickCount();

    init_nvs();
    load_midi_commands();

    init_oled();
    display_on = true;

    init_navigation_buttons();

    SemaphoreHandle_t signaling_sem = xSemaphoreCreateBinary();

    //====================================================
    //           TAREFAS DO USB MIDI
    //====================================================
    TaskHandle_t daemon_task_hdl;
    TaskHandle_t class_driver_task_hdl;
    TaskHandle_t button_task_hdl;
    TaskHandle_t navigation_task_hdl;
    TaskHandle_t power_management_hdl;

    // USB Host Daemon
    xTaskCreatePinnedToCore(
        host_lib_daemon_task,
        "daemon",
        4096,
        (void*)signaling_sem,
        DAEMON_TASK_PRIORITY,
        &daemon_task_hdl,
        0
    );

    // Driver USB MIDI
    xTaskCreatePinnedToCore(
        class_driver_task,
        "usb_midi_class",
        8192,
        (void*)signaling_sem,
        CLASS_TASK_PRIORITY,
        &class_driver_task_hdl,
        0
    );

    //====================================================
    //      Demais tarefas do projeto original
    //====================================================

    // Botões MIDI
    xTaskCreatePinnedToCore(
        button_check_task,
        "buttons",
        4096,
        NULL,
        BUTTON_TASK_PRIORITY,
        &button_task_hdl,
        1
    );

    // Navegação de menus
    xTaskCreatePinnedToCore(
        navigation_button_task,
        "navigation",
        4096,
        NULL,
        NAVIGATION_TASK_PRIORITY,
        &navigation_task_hdl,
        1
    );

    // Gerenciamento de energia
    xTaskCreatePinnedToCore(
        power_management_task,
        "pwr_mgmt",
        4096,
        NULL,
        POWER_TASK_PRIORITY,
        &power_management_hdl,
        1
    );

    ESP_LOGI(TAG, "System started - USB-only, no UART");

    // Mostra comandos carregados
    for (int i = 0; i < BUTTON_COUNT; i++) {
        ESP_LOGI(TAG, "Button %d CMD: %02X %02X %02X %02X",
                 i + 1,
                 current_commands[i].data[0],
                 current_commands[i].data[1],
                 current_commands[i].data[2],
                 current_commands[i].data[3]);
    }

    // Loop principal (baixa prioridade)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
