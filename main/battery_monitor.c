#include "battery_monitor.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "globals.h"
#include "oled_display.h"
#include "power_management.h"

#define BATTERY_CTRL_PIN   GPIO_NUM_2
#define BATTERY_ADC_PIN    ADC_CHANNEL_0
#define MIN_VOLTAGE        3.3f
#define MAX_VOLTAGE        4.2f

static const char *TAG = "BATTERY";
static adc_oneshot_unit_handle_t adc_handle = NULL;
static bool low_battery_warning_shown = false;
static uint8_t last_battery_percent = 100;

void battery_monitor_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BATTERY_CTRL_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(BATTERY_CTRL_PIN, 0);

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    adc_oneshot_chan_cfg_t channel_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, BATTERY_ADC_PIN, &channel_config));
    
    ESP_LOGI(TAG, "Inicializado");
}

static float battery_read_voltage(void) {
    gpio_set_level(BATTERY_CTRL_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    int adc_raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, BATTERY_ADC_PIN, &adc_raw));
    
    gpio_set_level(BATTERY_CTRL_PIN, 0);
    
    float voltage_at_adc = (adc_raw / 4095.0f) * 3.3f;
    return voltage_at_adc * 2.0f;
}

uint8_t battery_monitor_get_percentage(void) {
    float voltage = battery_read_voltage();
    
    if (voltage >= MAX_VOLTAGE) return 100;
    if (voltage <= MIN_VOLTAGE) return 0;
    
    float percent_float = ((voltage - MIN_VOLTAGE) / (MAX_VOLTAGE - MIN_VOLTAGE)) * 100.0f;
    return (uint8_t)(percent_float + 0.5f);
}

// Função para mostrar aviso de bateria baixa (tela cheia)
void battery_show_warning_once(void) {
    if (!display_initialized || !display_on) return;
    
    uint8_t battery_percent = battery_monitor_get_percentage();
    g_battery_percent = battery_percent;
    
    if (battery_percent <= LOW_BATTERY_THRESHOLD && !low_battery_warning_shown) {
        ESP_LOGW(TAG, "⚠️ Exibindo tela de bateria baixa: %d%%", battery_percent);
        low_battery_warning_shown = true;
        
        // Limpa a tela completamente
        ssd1306_clear_screen(&dev, false);
        
        // Mostra aviso em tela cheia (centralizado)
        ssd1306_display_text(&dev, 1, " LOW BATTERY!     ", 18, false);
        ssd1306_display_text(&dev, 2, "                  ", 16, false);
        ssd1306_display_text(&dev, 3, " Charge soon!     ", 18, false);
        ssd1306_display_text(&dev, 4, "                  ", 16, false);
        
        char msg[24];
        snprintf(msg, sizeof(msg), " %d%% remaining     ", battery_percent);
        ssd1306_display_text(&dev, 5, msg, 18, false);
        ssd1306_display_text(&dev, 6, "                  ", 16, false);
        ssd1306_display_text(&dev, 7, " Press any key    ", 18, false);
    }
}

// Função para restaurar a tela normal (chamada quando usuário interage)
void battery_restore_normal_screen(void) {
    if (low_battery_warning_shown) {
        ESP_LOGI(TAG, "Restaurando tela normal");
        low_battery_warning_shown = false;
        
        if (display_initialized && display_on) {
            // Limpa a tela e restaura o conteúdo original
            ssd1306_clear_screen(&dev, false);
            update_display_partial();
        }
    }
}

void battery_monitor_update(void) {
    uint8_t battery_percent = battery_monitor_get_percentage();
    ESP_LOGI(TAG, "Bateria: %d%%", battery_percent);
    
    g_battery_percent = battery_percent;
    
    // Se a bateria está abaixo do limiar E ainda não mostrou o aviso
    if (battery_percent <= LOW_BATTERY_THRESHOLD && !low_battery_warning_shown && display_on) {
        battery_show_warning_once();
    }
    
    // Se a bateria recuperou acima do limiar, reseta a flag
    if (battery_percent > LOW_BATTERY_THRESHOLD && low_battery_warning_shown) {
        ESP_LOGI(TAG, "✅ Bateria recuperou: %d%% - Resetando flag", battery_percent);
        low_battery_warning_shown = false;
    }
    
    last_battery_percent = battery_percent;
}

// Função chamada quando o display é reativado
void battery_check_and_warn(void) {
    ESP_LOGI(TAG, "Display reativado - verificando bateria");
    low_battery_warning_shown = false;  // Reseta para permitir nova verificação
    battery_show_warning_once();
}