#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "ssd1306.h"
#include "esp_pm.h"
#include "midi_class_driver_txrx.c"

static const char *TAG = "DAEMON";

#define DAEMON_TASK_PRIORITY    2
#define CLASS_TASK_PRIORITY     3

// Configuração dos 10 botões físicos
#define BUTTON_COUNT 10
#define BUTTON_GPIO_1           6
#define BUTTON_GPIO_2           7
#define BUTTON_GPIO_3           14
#define BUTTON_GPIO_4           15
#define BUTTON_GPIO_5           16
#define BUTTON_GPIO_6           17
#define BUTTON_GPIO_7           18
#define BUTTON_GPIO_8           21
#define BUTTON_GPIO_9           47
#define BUTTON_GPIO_10          48

static const int button_gpios[BUTTON_COUNT] = {
    BUTTON_GPIO_1, BUTTON_GPIO_2, BUTTON_GPIO_3, BUTTON_GPIO_4, BUTTON_GPIO_5,
    BUTTON_GPIO_6, BUTTON_GPIO_7, BUTTON_GPIO_8, BUTTON_GPIO_9, BUTTON_GPIO_10
};

#define BUTTON_ACTIVE_LEVEL     0

// Configuração dos botões de navegação
#define BTN_UP_GPIO             10
#define BTN_DOWN_GPIO           11
#define BTN_HASH_GPIO           12
#define BTN_STAR_GPIO           13

// Configuração I2C para OLED
#define I2C_SDA_GPIO            8
#define I2C_SCL_GPIO            9
#define I2C_RESET_GPIO          -1
#define OLED_WIDTH              128
#define OLED_HEIGHT             64

// Estrutura para armazenar comando MIDI
typedef struct {
    uint8_t data[4];
    char description[20];
} midi_command_t;

// Estados do menu (apenas 2 modos agora)
typedef enum {
    MODE_NORMAL,
    MODE_EDIT
} menu_mode_t;

// Variáveis globais
static midi_command_t current_commands[BUTTON_COUNT];
static int current_button = 0;
static menu_mode_t current_mode = MODE_NORMAL;
static int edit_byte_index = 0;      // Índice do byte sendo editado (0-3)
static int edit_nibble_index = 0;    // Índice do nibble sendo editado (0-1)
static SSD1306_t dev;
static bool display_initialized = false;
static midi_command_t edit_command;

// Variáveis para controle da rolagem no modo normal
static int scroll_offset = 0;
static const int VISIBLE_BUTTONS = 5; // Número de botões visíveis na tela

// Variáveis estáticas para controle de estado
static bool edit_initialized = false;


// Variáveis estáticas para debounce dos botões
static bool last_up_state = true;
static bool last_down_state = true;
static bool last_hash_state = true;
static bool last_star_state = true;

// === VARIÁVEIS DE POWER MANAGEMENT === 
static uint32_t last_cpu_activity_time = 0;        // Para CPU/power save
static uint32_t last_display_activity_time = 0; //  Para controle do display
static bool display_on = true;
static bool cpu_power_save_mode = false; 

// Protótipos de função
static void init_nvs(void);
static bool load_midi_commands(void);
static void save_midi_commands(void);
static void init_oled(void);
static void init_navigation_buttons(void);
static void init_midi_buttons(void);
static void update_display_partial(void);
static void handle_navigation(void);
static void button_check_task(void *arg);
static void navigation_button_task(void *arg);
static void increment_nibble(uint8_t *byte, int nibble);
static void decrement_nibble(uint8_t *byte, int nibble);
static void init_power_management(void);
static void power_management_task(void *arg);
static void set_cpu_power_save_mode(void);
static void set_cpu_full_performance_mode(void);
static void display_power_save(bool enable);
static void update_cpu_activity_time(void);
static void update_display_activity_time(void);

// Inicializa o NVS
static void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "NVS initialized");
}

// Carrega os comandos MIDI salvos na NVS
static bool load_midi_commands(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    err = nvs_open("midi_storage", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved MIDI commands found, using defaults");
        return false;
    }
    
    // Tenta carregar cada botão
    bool success = true;
    for (int button = 0; button < BUTTON_COUNT; button++) {
        for (int i = 0; i < 4; i++) {
            char key[15];
            snprintf(key, sizeof(key), "btn%d_byte%d", button, i);
            err = nvs_get_u8(nvs_handle, key, &current_commands[button].data[i]);
            if (err != ESP_OK) {
                success = false;
                // Define valores padrão
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
        // Inicializa todos os botões com valores padrão
        for (int i = 0; i < BUTTON_COUNT; i++) {
            current_commands[i].data[0] = 0x0B;
            current_commands[i].data[1] = 0xB0;
            current_commands[i].data[2] = 0x00;
            current_commands[i].data[3] = 0x00;
            snprintf(current_commands[i].description, sizeof(current_commands[i].description), "Button %d", i + 1);
        }
    }
    
    return success;
}

// Salva os comandos MIDI atuais na NVS
static void save_midi_commands(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    err = nvs_open("midi_storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }
    
    // Salva cada botão
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
    
    // Confirma a escrita
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing to NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "All MIDI commands saved successfully");
    }
    
    nvs_close(nvs_handle);
}

static void host_lib_daemon_task(void *arg)
{
    SemaphoreHandle_t signaling_sem = (SemaphoreHandle_t)arg;

    ESP_LOGI(TAG, "Installing USB Host Library");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    xSemaphoreGive(signaling_sem);
    vTaskDelay(10);

    while (1) {
        uint32_t event_flags;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "no clients available");
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "no devices connected");
        }
    }
}

static void init_oled(void)
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

static void init_navigation_buttons(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BTN_UP_GPIO) | (1ULL << BTN_DOWN_GPIO) | 
                       (1ULL << BTN_HASH_GPIO) | (1ULL << BTN_STAR_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    ESP_LOGI(TAG, "Navigation buttons initialized");
}

static void init_midi_buttons(void)
{
    uint64_t button_mask = 0;
    for (int i = 0; i < BUTTON_COUNT; i++) {
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
    
    ESP_LOGI(TAG, "MIDI buttons initialized");
}

static void increment_nibble(uint8_t *byte, int nibble) {
    if (nibble == 0) {
        // Incrementar nibble alto
        uint8_t high_nibble = (*byte >> 4) & 0x0F;
        high_nibble = (high_nibble + 1) & 0x0F;
        *byte = (high_nibble << 4) | (*byte & 0x0F);
    } else {
        // Incrementar nibble baixo
        uint8_t low_nibble = (*byte & 0x0F);
        low_nibble = (low_nibble + 1) & 0x0F;
        *byte = (*byte & 0xF0) | low_nibble;
    }
}

static void decrement_nibble(uint8_t *byte, int nibble) {
    if (nibble == 0) {
        // Decrementar nibble alto
        uint8_t high_nibble = (*byte >> 4) & 0x0F;
        high_nibble = (high_nibble - 1) & 0x0F;
        *byte = (high_nibble << 4) | (*byte & 0x0F);
    } else {
        // Decrementar nibble baixo
        uint8_t low_nibble = (*byte & 0x0F);
        low_nibble = (low_nibble - 1) & 0x0F;
        *byte = (*byte & 0xF0) | low_nibble;
    }
}

static void update_display_partial(void)
{
    switch (current_mode) {
        case MODE_NORMAL:
            // Modo normal - título fixo "BUTTON CONFIG" com seleção direta
            ssd1306_display_text(&dev, 0, "BUTTON CONFIG   ", 16, false);
            ssd1306_display_text(&dev, 1, "----------------", 16, false);
            
            // Mostra os botões visíveis com indicador de seleção (5 botões: linhas 2-6)
            for (int i = 0; i < VISIBLE_BUTTONS; i++) {
                int button_index = scroll_offset + i;
                char button_line[18];
                char *btn_ptr = button_line;
                
                if (button_index < BUTTON_COUNT) {
                    // ">" para botão selecionado, espaço para outros
                    *btn_ptr++ = (button_index == current_button) ? '>' : ' ';
                    
                    // "B" + número
                    *btn_ptr++ = 'B';
                    int btn_num = button_index + 1;
                    if (btn_num >= 10) {
                        *btn_ptr++ = '1';
                        *btn_ptr++ = '0' + (btn_num - 10);
                    } else {
                        *btn_ptr++ = '0' + btn_num;
                    }
                    
                    // ":" + bytes hex
                    *btn_ptr++ = ':';
                    for (int j = 0; j < 4; j++) {
                        uint8_t byte = current_commands[button_index].data[j];
                        *btn_ptr++ = "0123456789ABCDEF"[byte >> 4];
                        *btn_ptr++ = "0123456789ABCDEF"[byte & 0x0F];
                    }
                    *btn_ptr = '\0';
                    
                    ssd1306_display_text(&dev, 2 + i, button_line, strlen(button_line), false);
                } else {
                    // Linha vazia se não há botão
                    ssd1306_display_text(&dev, 2 + i, "                ", 16, false);
                }
            }
            
            // Instruções na última linha (linha 7)
            ssd1306_display_text(&dev, 7, "*:Edit          ", 16, false);
            break;
            
        case MODE_EDIT:
            // Modo edição - formato [0]BB00000 (edição por nibble)
            if (!edit_initialized) {           
                // Cabeçalho - construção manual para evitar warnings
                char title[16] = "                ";
                int btn_num = current_button + 1;

                if (btn_num >= 10) {
                    // "Edit Btn10" - 10 caracteres
                    memcpy(title, "Edit BT 10     ", 16); // 10 chars + null terminator
                    title[8] = '1';
                    title[9] = '0' + (btn_num - 10);
                } else {
                    // "Edit Btn 1" - 10 caracteres  
                    memcpy(title, "Edit BT 1      ", 16); // 10 chars + null terminator
                    title[7] = ' ';
                    title[8] = '0' + btn_num;
                }
                ssd1306_display_text(&dev, 0, title, strlen(title), false);
                ssd1306_display_text(&dev, 1, "----------------", 16, false);
                
                // Instruções
                ssd1306_display_text(&dev, 5, "Up/Dn:Change    ", 16, false);
                ssd1306_display_text(&dev, 6, "*:Next #:Save   ", 16, false);
                ssd1306_display_text(&dev, 7, "Hold#:Cancel    ", 16, false);
                
                edit_initialized = true;
                
                // Limpa APENAS as linhas que mostram conteúdo dos botões (2, 3, 4)
                ssd1306_display_text(&dev, 2, "                ", 16, false);
                ssd1306_display_text(&dev, 3, "                ", 16, false);
                ssd1306_display_text(&dev, 4, "                ", 16, false);
            }
            
            // Formata todos os bytes em uma linha com destaque no nibble atual
            char display_line[20];
            char *ptr = display_line;
            
            for (int byte_idx = 0; byte_idx < 4; byte_idx++) {
                uint8_t byte = edit_command.data[byte_idx];
                char nibble_high = "0123456789ABCDEF"[byte >> 4];
                char nibble_low = "0123456789ABCDEF"[byte & 0x0F];
                
                if (byte_idx == edit_byte_index) {
                    // Byte atual - destaca o nibble sendo editado
                    if (edit_nibble_index == 0) {
                        // Editando nibble alto: [X]Y
                        *ptr++ = '[';
                        *ptr++ = nibble_high;
                        *ptr++ = ']';
                        *ptr++ = nibble_low;
                    } else {
                        // Editando nibble baixo: X[Y]
                        *ptr++ = nibble_high;
                        *ptr++ = '[';
                        *ptr++ = nibble_low;
                        *ptr++ = ']';
                    }
                } else {
                    // Outros bytes normais: XY
                    *ptr++ = nibble_high;
                    *ptr++ = nibble_low;
                }
            }
            *ptr = '\0';
            
            // Atualiza a linha de edição (linha 3)
            ssd1306_display_text(&dev, 3, display_line, strlen(display_line), false);
            
            // Limpa a linha 4 (onde estava o decimal)
            ssd1306_display_text(&dev, 4, "                ", 16, false);
            break;
    }
}

static void handle_navigation(void)
{
    bool current_up = gpio_get_level(BTN_UP_GPIO);
    bool current_down = gpio_get_level(BTN_DOWN_GPIO);
    bool current_hash = gpio_get_level(BTN_HASH_GPIO);
    bool current_star = gpio_get_level(BTN_STAR_GPIO);
    
    // Só atualiza o timer quando há MUDANÇA DE ESTADO (botão pressionado)
    if ((last_up_state && !current_up) ||      // Botão UP pressionado
        (last_down_state && !current_down) ||  // Botão DOWN pressionado  
        (last_hash_state && !current_hash) ||  // Botão HASH pressionado
        (last_star_state && !current_star)) {  // Botão STAR pressionado
        
        update_cpu_activity_time();           // Atualiza timer geral
        update_display_activity_time();   // Atualiza timer do display
        ESP_LOGI(TAG, "Navigation button - both timers updated");
    }
    
    // Processa as ações apenas quando botões são PRESSIONADOS
    if (last_up_state && !current_up) {
        ESP_LOGI(TAG, "[ACTION] UP button pressed");
        switch (current_mode) {
            case MODE_NORMAL:
                if (current_button > 0) {
                    current_button--;
                    if (current_button < scroll_offset) {
                        scroll_offset = current_button;
                    }
                    update_display_partial();
                }
                break;
            case MODE_EDIT:
                increment_nibble(&edit_command.data[edit_byte_index], edit_nibble_index);
                update_display_partial();
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (last_down_state && !current_down) {
        ESP_LOGI(TAG, "[ACTION] DOWN button pressed");
        switch (current_mode) {
            case MODE_NORMAL:
                if (current_button < BUTTON_COUNT - 1) {
                    current_button++;
                    if (current_button >= scroll_offset + VISIBLE_BUTTONS) {
                        scroll_offset = current_button - VISIBLE_BUTTONS + 1;
                    }
                    update_display_partial();
                }
                break;
            case MODE_EDIT:
                decrement_nibble(&edit_command.data[edit_byte_index], edit_nibble_index);
                update_display_partial();
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (last_star_state && !current_star) {
        ESP_LOGI(TAG, "[ACTION] STAR button pressed");
        switch (current_mode) {
            case MODE_NORMAL:
                current_mode = MODE_EDIT;
                edit_byte_index = 0;
                edit_nibble_index = 0;
                memcpy(edit_command.data, current_commands[current_button].data, sizeof(edit_command.data));
                edit_initialized = false;
                update_display_partial();
                break;
            case MODE_EDIT:
                if (edit_nibble_index == 0) {
                    edit_nibble_index = 1;
                } else {
                    edit_nibble_index = 0;
                    edit_byte_index = (edit_byte_index + 1) % 4;
                }
                update_display_partial();
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (last_hash_state && !current_hash) {
        ESP_LOGI(TAG, "[ACTION] HASH button pressed");
        switch (current_mode) {
            case MODE_NORMAL:
                // Volta para o primeiro botão na página inicial
                if (current_button != 0) {
                    current_button = 0;
                    scroll_offset = 0;
                    update_display_partial();
                    ESP_LOGI(TAG, "HASH: Returned to first button");
                } else {
                    ESP_LOGI(TAG, "HASH: Already at first button");
                }
                break;
            case MODE_EDIT:
                uint32_t press_start_time = xTaskGetTickCount();
                
                while (!gpio_get_level(BTN_HASH_GPIO)) {
                    if ((xTaskGetTickCount() - press_start_time) > pdMS_TO_TICKS(1000)) {
                        current_mode = MODE_NORMAL;
                        edit_initialized = false;
                        update_display_partial();
                        vTaskDelay(pdMS_TO_TICKS(300));
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                
                if ((xTaskGetTickCount() - press_start_time) <= pdMS_TO_TICKS(1000)) {
                    memcpy(current_commands[current_button].data, edit_command.data, sizeof(edit_command.data));
                    save_midi_commands();
                    current_mode = MODE_NORMAL;
                    edit_initialized = false;
                    update_display_partial();
                }
                break;
            default:
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // ATUALIZA os estados anteriores
    last_up_state = current_up;
    last_down_state = current_down;
    last_hash_state = current_hash;
    last_star_state = current_star;
}

static void button_check_task(void *arg)
{
    init_midi_buttons();
    
    ESP_LOGI(TAG, "Button controller ready");

    bool last_button_states[BUTTON_COUNT];
    uint32_t last_send_times[BUTTON_COUNT];
    
    // Inicializa estados dos botões
    for (int i = 0; i < BUTTON_COUNT; i++) {
        last_button_states[i] = true;
        last_send_times[i] = 0;
    }
    
    const uint32_t DEBOUNCE_DELAY = pdMS_TO_TICKS(100);

    while (1) {
                
        for (int i = 0; i < BUTTON_COUNT; i++) {
            bool current_state = gpio_get_level(button_gpios[i]);
            uint32_t current_time = xTaskGetTickCount();
            
            if (last_button_states[i] && !current_state) {
                if ((current_time - last_send_times[i]) >= DEBOUNCE_DELAY) {
                    // Atividade detectada
                    update_cpu_activity_time(); 
                    ESP_LOGI(TAG, "MIDI Button %d - only CPU power timer updated", i+1);
                    if (cpu_power_save_mode) {
                        set_cpu_full_performance_mode();                        
                    }
                    // Atualiza botão atual se necessário
                    if(display_on && current_mode == MODE_NORMAL){
                        if (current_button != i) {
                            current_button = i;
                            // Ajusta o scroll_offset para mostrar o botão atual
                            if (i < scroll_offset) {
                                scroll_offset = i;
                            } else if (i >= scroll_offset + VISIBLE_BUTTONS) {
                                scroll_offset = i - VISIBLE_BUTTONS + 1;
                            }
                            update_display_partial();
                        }
                    }
                    
                    ESP_LOGI(TAG, "Button %d SENDING: %02X %02X %02X %02X", i + 1,
                            current_commands[i].data[0], current_commands[i].data[1],
                            current_commands[i].data[2], current_commands[i].data[3]);
                    
                    if (midi_driver_ready_for_tx()) {
                        midi_send_data(current_commands[i].data, sizeof(current_commands[i].data));
                        
                        //char feedback[25];
                        //snprintf(feedback, sizeof(feedback), "BTN%d SENT!", i + 1);
                        //ssd1306_display_text(&dev, 7, feedback, strlen(feedback), false);
                        
                    } else {
                        ESP_LOGI(TAG, "Driver not ready");
                        //char feedback[25];
                        //snprintf(feedback, sizeof(feedback), "BTN%d ERROR!", i + 1);
                        //ssd1306_display_text(&dev, 7, feedback, strlen(feedback), false);
                    }
                    
                    last_send_times[i] = current_time;
                }
            }
            
            last_button_states[i] = current_state;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void navigation_button_task(void *arg)
{
    ESP_LOGI(TAG, "Navigation task started - FIXED VERSION");
    
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50)); // Mantém 50ms para responsividade
        
        handle_navigation(); // só atualiza timer quando realmente há pressionamento
    }
}

// power save
static void init_power_management(void)
{
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 40,
        .light_sleep_enable = false
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
}

static void set_cpu_power_save_mode(void)
{
    ESP_LOGI(TAG, "Entering power save mode");
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 40,
        .light_sleep_enable = true
    };
    esp_pm_configure(&pm_config);
    cpu_power_save_mode = true;
}

static void set_cpu_full_performance_mode(void)
{
    ESP_LOGI(TAG, "Entering full performance mode");
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 40,
        .light_sleep_enable = false
    };
    esp_pm_configure(&pm_config);
    cpu_power_save_mode = false;    
}

static void display_power_save(bool enable)
{
    if (!display_initialized) return;
    
    if (enable) {
        // "Ligar" o display - limpar e mostrar conteúdo
        ssd1306_clear_screen(&dev, false);
        update_display_partial();
        display_on = true;
        ESP_LOGI(TAG, " Display ON - Standby exited");
    } else {
        // "Desligar" o display - limpar completamente
        ssd1306_clear_screen(&dev, false);  // Toda tela preta
        // Opcional: mostrar mensagem de standby
        ssd1306_display_text(&dev, 3, "   STANDBY...   ", 16, false);
        display_on = false;
        ESP_LOGI(TAG, " Display OFF - Standby mode");
    }
}

static void power_management_task(void *arg)
{
    ESP_LOGI(TAG, "=== SEPARATED CONTROL - CPU vs DISPLAY ===");
    
    // INICIALIZA ambos os timers
    last_cpu_activity_time = xTaskGetTickCount();
    last_display_activity_time = xTaskGetTickCount();
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        uint32_t current_time = xTaskGetTickCount();
        
        // TIMER 1: Power Save (CPU) - considera TODOS os botões
        uint32_t cpu_inactive_ms = (current_time - last_cpu_activity_time) * portTICK_PERIOD_MS;
        
        // TIMER 2: Display - considera APENAS navegação
        uint32_t display_inactive_ms = (current_time - last_display_activity_time) * portTICK_PERIOD_MS;
        
        ESP_LOGI(TAG, "[CONTROL] CPU: %d ms, Display: %d ms, CPU-Save: %s, Display-On: %s", 
                 cpu_inactive_ms, display_inactive_ms, 
                 cpu_power_save_mode ? "YES" : "NO",
                 display_on ? "YES" : "NO");

        // CONTROLE 1: POWER SAVE (CPU) - após 10 segundos sem NENHUM botão
        if (!cpu_power_save_mode && cpu_inactive_ms > 10000) {
            ESP_LOGI(TAG, "💡 CPU POWER SAVE MODE ACTIVATED");
            set_cpu_power_save_mode();
        }
        
        // CONTROLE 2: DISPLAY - após 15 segundos sem NAVEGAÇÃO
        if (display_inactive_ms > 15000 && display_on) {
            if (current_mode == MODE_EDIT) {
                ESP_LOGI(TAG, "🖥️ AUTO-CANCEL EDIT MODE (standby timeout)");
                current_mode = MODE_NORMAL;
                edit_initialized = false;
            }
            ESP_LOGI(TAG, "🖥️ DISPLAY STANDBY (no navigation)");
            display_power_save(false);
        }
        
        // REATIVAÇÃO: Se há navegação recente E display está off
        if (display_inactive_ms < 1000 && !display_on) {
            ESP_LOGI(TAG, "🖥️ DISPLAY REACTIVATED (navigation detected)");
            display_power_save(true);
        }
        
        // REATIVAÇÃO: Se há qualquer atividade recente E está em power save
        if (cpu_inactive_ms < 1000 && cpu_power_save_mode) {
            ESP_LOGI(TAG, "💡 CPU POWER SAVE EXITED (activity detected)");
            set_cpu_full_performance_mode();
        }
    }
}

//FUNÇÃO para atualizar atividade geral (CPU/power save)
static void update_cpu_activity_time(void) {
    last_cpu_activity_time = xTaskGetTickCount();
}

// FUNÇÃO para atualizar atividade do display
static void update_display_activity_time(void) {
    last_display_activity_time = xTaskGetTickCount();
    // Se o display estava off, liga ele
    if (!display_on) {
        display_power_save(true);
    }
}

void app_main(void)
{
    init_power_management();
    last_cpu_activity_time = xTaskGetTickCount();
    last_display_activity_time = xTaskGetTickCount();
    ESP_LOGI(TAG, "Separated timers initialized");
    ESP_LOGI(TAG, "Activity timer initialized to: %d", last_cpu_activity_time);

    init_nvs();
    load_midi_commands();

    SemaphoreHandle_t signaling_sem = xSemaphoreCreateBinary();

    init_oled();
    display_on = true;
    init_navigation_buttons();
    
    TaskHandle_t daemon_task_hdl;
    TaskHandle_t class_driver_task_hdl;
    TaskHandle_t button_task_hdl;
    TaskHandle_t navigation_task_hdl;
    TaskHandle_t power_management_hdl; 
    
    xTaskCreatePinnedToCore(host_lib_daemon_task, "daemon", 4096, (void *)signaling_sem, DAEMON_TASK_PRIORITY, &daemon_task_hdl, 0);
    xTaskCreatePinnedToCore(class_driver_task, "class", 4096, (void *)signaling_sem, CLASS_TASK_PRIORITY, &class_driver_task_hdl, 0);
    xTaskCreatePinnedToCore(button_check_task, "button", 4096, NULL, CLASS_TASK_PRIORITY, &button_task_hdl, 1);
    xTaskCreatePinnedToCore(navigation_button_task, "navigation", 4096, NULL, CLASS_TASK_PRIORITY, &navigation_task_hdl, 1);
    //xTaskCreatePinnedToCore(power_management_task, "pwr_mgmt", 2048, NULL, 1, &power_management_hdl, 1);
    if (xTaskCreatePinnedToCore(power_management_task, "pwr_mgmt", 4096, NULL, 1, &power_management_hdl, 1) == pdPASS) {
        ESP_LOGI(TAG, "Power management task CREATED SUCCESSFULLY");
    } else {
        ESP_LOGE(TAG, "FAILED to create power management task");
    }

    ESP_LOGI(TAG, "System started - Power Management Debug Mode");
    //vTaskDelay(100);

    ESP_LOGI(TAG, "System started successfully with power management!");
    for (int i = 0; i < BUTTON_COUNT; i++) {
        ESP_LOGI(TAG, "Button %d: %02X %02X %02X %02X", i + 1,
                current_commands[i].data[0], current_commands[i].data[1],
                current_commands[i].data[2], current_commands[i].data[3]);
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        //ESP_LOGI(TAG, "[MAIN] System still running...");
    }
}