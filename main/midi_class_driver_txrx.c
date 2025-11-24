/*
 * MIDI USB Host Driver (keeps original driver behavior)
 * Added a public hook: process_usb_rx_for_uart() which by default is a no-op.
 * If an application or the midi_uart library implements this function, incoming
 * USB MIDI data will be forwarded to it for UART transmission.
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "esp_mac.h"

#include "midi_class_driver_txrx.h"

// Hook into UART queueing provided by midi_uart
// Returns true if the packet was queued successfully (non-blocking), false otherwise
extern bool midi_uart_try_enqueue_usb(const uint8_t *data, size_t length);
extern void midi_uart_send_to_uart(const uint8_t *data, size_t length);

#define USB_CLIENT_NUM_EVENT_MSG    5
#define MIDI_MESSAGE_LENGTH         4
#define MIDI_TX_QUEUE_SIZE          20

// MIDI USB Class and Subclass definitions
#define USB_CLASS_AUDIO             0x01
#define USB_SUBCLASS_AUDIOCONTROL   0x01
#define USB_SUBCLASS_MIDISTREAM     0x03

// Action definitions
#define ACTION_OPEN_DEV             0x01
#define ACTION_GET_DEV_INFO         0x02
#define ACTION_GET_DEV_DESC         0x04
#define ACTION_GET_CONFIG_DESC      0x08
#define ACTION_GET_STR_DESC         0x10
#define ACTION_CLAIM_INTERFACE      0x20
#define ACTION_START_READING_DATA   0x40
#define ACTION_PREPARE_SEND_DATA    0x80
#define ACTION_CLOSE_DEV            0x100
#define ACTION_EXIT                 0x200

typedef struct {
    uint8_t interface_nmbr;
    uint8_t alternate_setting;
    uint8_t endpoint_in_address;     // Endpoint para receber dados
    uint8_t endpoint_out_address;    // Endpoint para enviar dados
    uint8_t max_packet_size_in;
    uint8_t max_packet_size_out;
} interface_config_t;

typedef struct {
    uint8_t data[MIDI_MESSAGE_LENGTH];
    size_t length;
} internal_midi_message_t;

typedef struct {
    usb_host_client_handle_t client_hdl;
    uint8_t dev_addr;
    usb_device_handle_t dev_hdl;
    uint32_t actions;
    interface_config_t interface_conf;
    usb_transfer_t *rx_transfer;     // Transferência para recepção
    QueueHandle_t tx_queue;          // Fila para mensagens a serem enviadas
    bool ready_for_tx;               // Flag indicando se está pronto para enviar
} class_driver_t;

static const char *DRIVER_TAG = "MIDI_DRIVER_TXRX";

// Variável global para rastrear a instância do driver
static class_driver_t *global_driver_instance = NULL;

/*
 * Public hook:
 * By default this function does nothing. If the midi_uart library (or the app)
 * provides an implementation, it will be called to forward USB-received MIDI
 * packets to the UART for MIDI-OUT.
 */
void __attribute__((weak)) process_usb_rx_for_uart(const uint8_t *data, size_t length)
{
    (void)data;
    (void)length;
    // default: no-op
}

// Callback para recepção de dados MIDI
static void midi_usb_host_rx_callback(usb_transfer_t *transfer) {
    class_driver_t *driver_obj = (class_driver_t *)transfer->context;
    int size = (int)transfer->actual_num_bytes;
    
    // Uma mensagem contém 4 bytes de dados
    int num_messages = size / MIDI_MESSAGE_LENGTH;
    int offset = 0;

    // Processar mensagens recebidas
    if(size > 0) {
        ESP_LOGI(DRIVER_TAG, "Received %d bytes (%d messages)", size, num_messages);
        
        // Print cada mensagem separadamente (debug)
        for(int i = 0; i < num_messages; i++) {
            ESP_LOGD(DRIVER_TAG, "MIDI[%d]: %02X %02X %02X %02X", i,
                    transfer->data_buffer[offset],
                    transfer->data_buffer[offset + 1],
                    transfer->data_buffer[offset + 2],
                    transfer->data_buffer[offset + 3]);
            offset += MIDI_MESSAGE_LENGTH;
        }

        // Try to enqueue USB data to the UART queue (non-blocking) for low-latency forwarding.
        // If queue is full or not available, fallback to direct send to UART to avoid loss.
        if (midi_uart_try_enqueue_usb(transfer->data_buffer, size) == false) {
            // fallback direct send to UART
            midi_uart_send_to_uart(transfer->data_buffer, size);
        }
    }

    // Re-submeter a transferência para continuar recebendo dados
    if (driver_obj != NULL && driver_obj->dev_hdl != NULL) {
        esp_err_t err = usb_host_transfer_submit(transfer);
        if (err != ESP_OK) {
            if (err == ESP_ERR_INVALID_STATE) {
                ESP_LOGW(DRIVER_TAG, "USB device disconnected or in invalid state, cannot re-submit RX transfer");
                // Não tentar re-submeter
                return;
            } else {
                ESP_LOGE(DRIVER_TAG, "Failed to re-submit RX transfer: %s", esp_err_to_name(err));
                // don't abort the whole system here
            }
        }
    }
}

// Callback para transmissão de dados MIDI
static void midi_usb_host_tx_callback(usb_transfer_t *transfer) {
    ESP_LOGI(DRIVER_TAG, "TX CALLBACK: Status = %d, Actual Bytes = %d", 
             transfer->status, transfer->actual_num_bytes);
    
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGI(DRIVER_TAG, "MIDI data sent successfully!");
    } else {
        ESP_LOGE(DRIVER_TAG, "MIDI transfer failed with status: %d", transfer->status);
    }
    
    // Liberar a transferência após o envio
    usb_host_transfer_free(transfer);
}

// Função para processar a fila de transmissão
static void process_tx_queue(class_driver_t *driver_obj) {
    if (driver_obj == NULL || !driver_obj->ready_for_tx) {
        return;
    }

    // Verificação adicional: dispositivo ainda válido?
    if (driver_obj->dev_hdl == NULL) {
        ESP_LOGW(DRIVER_TAG, "Cannot process TX queue: device handle is NULL");
        return;
    }

    internal_midi_message_t message;
    
    // Verificar se há mensagens na fila para enviar
    while (xQueueReceive(driver_obj->tx_queue, &message, 0) == pdTRUE) {
        ESP_LOGI(DRIVER_TAG, "Processing TX queue: %d bytes", message.length);
        
        // Alocar transferência para envio
        usb_transfer_t *transfer;
        esp_err_t err = usb_host_transfer_alloc(message.length, 0, &transfer);
        
        if (err != ESP_OK) {
            ESP_LOGE(DRIVER_TAG, "Failed to allocate transfer for TX: %d", err);
            continue;
        }

        // Configurar a transferência
        memcpy(transfer->data_buffer, message.data, message.length);
        transfer->num_bytes = message.length;
        transfer->callback = midi_usb_host_tx_callback;
        transfer->bEndpointAddress = driver_obj->interface_conf.endpoint_out_address;
        transfer->device_handle = driver_obj->dev_hdl;

        ESP_LOGI(DRIVER_TAG, "Submitting USB transfer to endpoint 0x%02X", transfer->bEndpointAddress);

        // Enviar dados com verificação de erro
        err = usb_host_transfer_submit(transfer);
        if (err != ESP_OK) {
            if (err == ESP_ERR_INVALID_STATE) {
                ESP_LOGW(DRIVER_TAG, "Cannot submit TX transfer: device disconnected");
            } else {
                ESP_LOGE(DRIVER_TAG, "Failed to submit TX transfer: %d", err);
            }
            usb_host_transfer_free(transfer);
        } else {
            ESP_LOGI(DRIVER_TAG, "USB transfer submitted successfully");
        }
    }
}

// Analisar configurações da interface MIDI
static void get_midi_interface_settings(const usb_config_desc_t *usb_conf, interface_config_t *interface_conf) {
    assert(usb_conf != NULL);
    assert(interface_conf != NULL);

    ESP_LOGI(DRIVER_TAG, "Getting MIDI interface configuration");

    int offset = 0;
    uint16_t wTotalLength = usb_conf->wTotalLength;
    const usb_standard_desc_t *next_desc = (const usb_standard_desc_t *)usb_conf;

    // Reset da configuração
    memset(interface_conf, 0, sizeof(interface_config_t));

    do {
        if(next_desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            usb_intf_desc_t *interface_desc = (usb_intf_desc_t *)next_desc;

            // Verificar se é uma interface MIDI
            if(interface_desc->bInterfaceClass == USB_CLASS_AUDIO && 
               interface_desc->bInterfaceSubClass == USB_SUBCLASS_MIDISTREAM) {
                ESP_LOGI(DRIVER_TAG, "Found MIDI Stream Interface");

                if(interface_desc->bNumEndpoints >= 2) {
                    interface_conf->interface_nmbr = interface_desc->bInterfaceNumber;
                    interface_conf->alternate_setting = interface_desc->bAlternateSetting;

                    ESP_LOGI(DRIVER_TAG, "MIDI Interface: Number=%d, Alternate=%d", 
                            interface_conf->interface_nmbr, interface_conf->alternate_setting);
                }
            }
        }

        if(next_desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
            usb_ep_desc_t *ep_desc = (usb_ep_desc_t *)next_desc;
            uint8_t ep_addr = ep_desc->bEndpointAddress;
            uint8_t ep_dir = USB_EP_DESC_GET_EP_DIR(ep_desc);

            if(ep_dir) { // IN endpoint (recepção do host)
                interface_conf->endpoint_in_address = ep_addr;
                interface_conf->max_packet_size_in = ep_desc->wMaxPacketSize & 0x7FF;
                ESP_LOGI(DRIVER_TAG, "IN Endpoint: Address=0x%02X, MaxPacketSize=%d", 
                        interface_conf->endpoint_in_address, interface_conf->max_packet_size_in);
            } else { // OUT endpoint (transmissão do host)
                interface_conf->endpoint_out_address = ep_addr;
                interface_conf->max_packet_size_out = ep_desc->wMaxPacketSize & 0x7FF;
                ESP_LOGI(DRIVER_TAG, "OUT Endpoint: Address=0x%02X, MaxPacketSize=%d", 
                        interface_conf->endpoint_out_address, interface_conf->max_packet_size_out);
            }
        }

        next_desc = usb_parse_next_descriptor(next_desc, wTotalLength, &offset);

    } while (next_desc != NULL);

    ESP_LOGI(DRIVER_TAG, "MIDI Interface Analysis Complete:");
    ESP_LOGI(DRIVER_TAG, "  - IN Endpoint: 0x%02X", interface_conf->endpoint_in_address);
    ESP_LOGI(DRIVER_TAG, "  - OUT Endpoint: 0x%02X", interface_conf->endpoint_out_address);
    ESP_LOGI(DRIVER_TAG, "  - Interface: %d", interface_conf->interface_nmbr);
}

// Callback de eventos do cliente USB
static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg) {
    class_driver_t *driver_obj = (class_driver_t *)arg;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        if (driver_obj->dev_addr == 0) {
            driver_obj->dev_addr = event_msg->new_dev.address;
            driver_obj->actions |= ACTION_OPEN_DEV;
            ESP_LOGI(DRIVER_TAG, "New device detected at address %d", driver_obj->dev_addr);
        }
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        if (driver_obj->dev_hdl != NULL) {
            driver_obj->actions = ACTION_CLOSE_DEV;
            ESP_LOGI(DRIVER_TAG, "Device disconnected");
        }
        break;
    default:
        abort();
    }
}

// Ação: Abrir dispositivo
static void action_open_dev(class_driver_t *driver_obj) {
    assert(driver_obj->dev_addr != 0);
    ESP_LOGI(DRIVER_TAG, "Opening device at address %d", driver_obj->dev_addr);
    ESP_ERROR_CHECK(usb_host_device_open(driver_obj->client_hdl, driver_obj->dev_addr, &driver_obj->dev_hdl));
    driver_obj->actions &= ~ACTION_OPEN_DEV;
    driver_obj->actions |= ACTION_GET_DEV_INFO;
}

// Ação: Obter informações do dispositivo
static void action_get_info(class_driver_t *driver_obj) {
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(DRIVER_TAG, "Getting device information");
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(driver_obj->dev_hdl, &dev_info));
    ESP_LOGI(DRIVER_TAG, "\t%s speed", (dev_info.speed == USB_SPEED_LOW) ? "Low" : "Full");
    ESP_LOGI(DRIVER_TAG, "\tbConfigurationValue %d", dev_info.bConfigurationValue);
    driver_obj->actions &= ~ACTION_GET_DEV_INFO;
    driver_obj->actions |= ACTION_GET_DEV_DESC;
}

// Ação: Obter descritor do dispositivo
static void action_get_dev_desc(class_driver_t *driver_obj) {
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(DRIVER_TAG, "Getting device descriptor");
    const usb_device_desc_t *dev_desc;
    ESP_ERROR_CHECK(usb_host_get_device_descriptor(driver_obj->dev_hdl, &dev_desc));
    usb_print_device_descriptor(dev_desc);
    driver_obj->actions &= ~ACTION_GET_DEV_DESC;
    driver_obj->actions |= ACTION_GET_CONFIG_DESC;
}

// Ação: Obter descritor de configuração
static void action_get_config_desc(class_driver_t *driver_obj) {
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(DRIVER_TAG, "Getting config descriptor");
    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(driver_obj->dev_hdl, &config_desc));
    usb_print_config_descriptor(config_desc, NULL);

    interface_config_t interface_config = {0};
    get_midi_interface_settings(config_desc, &interface_config);
    driver_obj->interface_conf = interface_config;

    driver_obj->actions &= ~ACTION_GET_CONFIG_DESC;
    driver_obj->actions |= ACTION_GET_STR_DESC;
}

// Ação: Obter descritores de string
static void action_get_str_desc(class_driver_t *driver_obj) {
    assert(driver_obj->dev_hdl != NULL);
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(driver_obj->dev_hdl, &dev_info));
    if (dev_info.str_desc_manufacturer) {
        ESP_LOGI(DRIVER_TAG, "Getting Manufacturer string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_manufacturer);
    }
    if (dev_info.str_desc_product) {
        ESP_LOGI(DRIVER_TAG, "Getting Product string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_product);
    }
    if (dev_info.str_desc_serial_num) {
        ESP_LOGI(DRIVER_TAG, "Getting Serial Number string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_serial_num);
    }
    driver_obj->actions &= ~ACTION_GET_STR_DESC;
    driver_obj->actions |= ACTION_CLAIM_INTERFACE;
}

// Ação: Claim da interface
static void action_claim_interface(class_driver_t *driver_obj) {
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(DRIVER_TAG, "Claiming MIDI Interface %d", driver_obj->interface_conf.interface_nmbr);

    ESP_ERROR_CHECK(usb_host_interface_claim(
            driver_obj->client_hdl,
            driver_obj->dev_hdl,
            driver_obj->interface_conf.interface_nmbr,
            driver_obj->interface_conf.alternate_setting));

    driver_obj->actions &= ~ACTION_CLAIM_INTERFACE;
    driver_obj->actions |= ACTION_START_READING_DATA;
}

// Ação: Iniciar leitura de dados
static void action_start_reading_data(class_driver_t *driver_obj) {
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(DRIVER_TAG, "Starting MIDI data reception");

    // Configurar transferência de recepção
    ESP_ERROR_CHECK(usb_host_transfer_alloc(1024, 0, &driver_obj->rx_transfer));

    driver_obj->rx_transfer->num_bytes = driver_obj->interface_conf.max_packet_size_in;
    driver_obj->rx_transfer->callback = midi_usb_host_rx_callback;
    driver_obj->rx_transfer->bEndpointAddress = driver_obj->interface_conf.endpoint_in_address;
    driver_obj->rx_transfer->device_handle = driver_obj->dev_hdl;
    driver_obj->rx_transfer->context = (void *)driver_obj;

    // Iniciar recepção contínua
    ESP_ERROR_CHECK(usb_host_transfer_submit(driver_obj->rx_transfer));
    ESP_LOGI(DRIVER_TAG, "MIDI reception started");

    driver_obj->actions &= ~ACTION_START_READING_DATA;
    driver_obj->actions |= ACTION_PREPARE_SEND_DATA;
}

// Ação: Preparar envio de dados
static void action_prepare_send_data(class_driver_t *driver_obj) {
    ESP_LOGI(DRIVER_TAG, "Preparing MIDI transmission");

    // Verificar se temos um endpoint OUT para transmissão
    if (driver_obj->interface_conf.endpoint_out_address == 0) {
        ESP_LOGW(DRIVER_TAG, "No OUT endpoint found - device is read-only");
        driver_obj->ready_for_tx = false;
        driver_obj->actions &= ~ACTION_PREPARE_SEND_DATA;
        return;
    }

    // Criar fila para mensagens de transmissão
    driver_obj->tx_queue = xQueueCreate(MIDI_TX_QUEUE_SIZE, sizeof(internal_midi_message_t));
    if (driver_obj->tx_queue == NULL) {
        ESP_LOGE(DRIVER_TAG, "Failed to create TX queue");
        driver_obj->ready_for_tx = false;
    } else {
        driver_obj->ready_for_tx = true;
        ESP_LOGI(DRIVER_TAG, "MIDI transmission ready - Device can send and receive");
    }

    // Atualizar instância global
    global_driver_instance = driver_obj;

    driver_obj->actions &= ~ACTION_PREPARE_SEND_DATA;
}

// Ação: Fechar dispositivo
static void action_close_dev(class_driver_t *driver_obj) {
    ESP_LOGI(DRIVER_TAG, "Closing MIDI device");

    driver_obj->ready_for_tx = false;

    // Liberar recursos de transmissão
    if (driver_obj->tx_queue != NULL) {
        // Limpar a fila antes de deletar
        internal_midi_message_t dummy;
        while (xQueueReceive(driver_obj->tx_queue, &dummy, 0) == pdTRUE) {
            // Simplesmente esvaziar a fila
        }
        vQueueDelete(driver_obj->tx_queue);
        driver_obj->tx_queue = NULL;
    }

    // Liberar transferência de recepção
    if (driver_obj->rx_transfer != NULL) {
        usb_host_transfer_free(driver_obj->rx_transfer);
        driver_obj->rx_transfer = NULL;
    }

    // Liberar interface
    if (driver_obj->dev_hdl != NULL) {
        ESP_ERROR_CHECK(usb_host_interface_release(
                driver_obj->client_hdl,
                driver_obj->dev_hdl,
                driver_obj->interface_conf.interface_nmbr));

        // Fechar dispositivo
        ESP_ERROR_CHECK(usb_host_device_close(driver_obj->client_hdl, driver_obj->dev_hdl));
    }

    driver_obj->dev_hdl = NULL;
    driver_obj->dev_addr = 0;

    // Limpar instância global
    global_driver_instance = NULL;

    driver_obj->actions &= ~ACTION_CLOSE_DEV;
    driver_obj->actions |= ACTION_EXIT;
}

// Função principal da tarefa do driver
void class_driver_task(void *arg)
{
    SemaphoreHandle_t signaling_sem = (SemaphoreHandle_t)arg;
    class_driver_t driver_obj = {0};

    // Registrar a instância globalmente desde o início
    global_driver_instance = &driver_obj;
    ESP_LOGI(DRIVER_TAG, "Driver task started, instance registered");

    //Wait until daemon task has installed USB Host Library
    xSemaphoreTake(signaling_sem, portMAX_DELAY);

    ESP_LOGI(DRIVER_TAG, "Registering Client");
    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = USB_CLIENT_NUM_EVENT_MSG,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = (void *) &driver_obj,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &driver_obj.client_hdl));

    // Variável para controlar o tempo do processamento TX
    TickType_t last_tx_process_time = xTaskGetTickCount();
    const TickType_t tx_process_interval = pdMS_TO_TICKS(10);

    while (1) {
        if (driver_obj.actions == 0) {
            usb_host_client_handle_events(driver_obj.client_hdl, tx_process_interval);
        } else {
            if (driver_obj.actions & ACTION_OPEN_DEV) {
                action_open_dev(&driver_obj);
            }
            if (driver_obj.actions & ACTION_GET_DEV_INFO) {
                action_get_info(&driver_obj);
            }
            if (driver_obj.actions & ACTION_GET_DEV_DESC) {
                action_get_dev_desc(&driver_obj);
            }
            if (driver_obj.actions & ACTION_GET_CONFIG_DESC) {
                action_get_config_desc(&driver_obj);
            }
            if (driver_obj.actions & ACTION_GET_STR_DESC) {
                action_get_str_desc(&driver_obj);
            }
            if(driver_obj.actions & ACTION_CLAIM_INTERFACE) {
                action_claim_interface(&driver_obj);
            }
            if(driver_obj.actions & ACTION_START_READING_DATA) {
                action_start_reading_data(&driver_obj);
            }
            if(driver_obj.actions & ACTION_PREPARE_SEND_DATA) {
                action_prepare_send_data(&driver_obj);
            }
            if (driver_obj.actions & ACTION_CLOSE_DEV) {
                action_close_dev(&driver_obj);
            }
            if (driver_obj.actions & ACTION_EXIT) {
                driver_obj.actions = 0;
            }
        }

        // Processar fila TX a cada iteração
        if (driver_obj.ready_for_tx) {
            process_tx_queue(&driver_obj);
        }

        // Processamento adicional periódico da fila TX
        TickType_t current_time = xTaskGetTickCount();
        if ((current_time - last_tx_process_time) >= tx_process_interval) {
            if (driver_obj.ready_for_tx) {
                process_tx_queue(global_driver_instance);
            }
            last_tx_process_time = current_time;
        }
    }
}

// ============================================================================
// API PÚBLICA PARA ENVIO DE DADOS MIDI
// ============================================================================

// Função para verificar se o driver está pronto para transmissão
bool midi_driver_ready_for_tx(void) {
    if (global_driver_instance == NULL) {
        return false;
    }

    return global_driver_instance->ready_for_tx;
}

// Função para obter o estado detalhado do driver
void midi_driver_print_status(void) {
    if (global_driver_instance == NULL) {
        ESP_LOGI(DRIVER_TAG, "Driver status: NO INSTANCE");
        return;
    }

    ESP_LOGI(DRIVER_TAG, "=== MIDI Driver Status ===");
    ESP_LOGI(DRIVER_TAG, "  - Instance: %p", global_driver_instance);
    ESP_LOGI(DRIVER_TAG, "  - Device handle: %p", global_driver_instance->dev_hdl);
    ESP_LOGI(DRIVER_TAG, "  - Ready for TX: %s", 
             global_driver_instance->ready_for_tx ? "YES" : "NO");
    ESP_LOGI(DRIVER_TAG, "  - TX queue: %p", global_driver_instance->tx_queue);
    ESP_LOGI(DRIVER_TAG, "  - OUT Endpoint: 0x%02X", 
             global_driver_instance->interface_conf.endpoint_out_address);
    ESP_LOGI(DRIVER_TAG, "===========================");
}

// Função para enviar dados MIDI brutos
bool midi_send_data(const uint8_t *data, size_t length) {
    ESP_LOGI(DRIVER_TAG, "midi_send_data called: length=%d", length);

    if (global_driver_instance == NULL) {
        ESP_LOGE(DRIVER_TAG, "midi_send_data: No driver instance");
        return false;
    }

    if (!global_driver_instance->ready_for_tx) {
        ESP_LOGE(DRIVER_TAG, "midi_send_data: Driver not ready for TX");
        return false;
    }

    // Verificação adicional: dispositivo ainda conectado?
    if (global_driver_instance->dev_hdl == NULL) {
        ESP_LOGW(DRIVER_TAG, "midi_send_data: Device not connected");
        return false;
    }

    if (data == NULL || length == 0) {
        ESP_LOGE(DRIVER_TAG, "midi_send_data: Invalid data");
        return false;
    }

    ESP_LOGI(DRIVER_TAG, "Data: %02X %02X %02X %02X", 
             data[0], data[1], data[2], data[3]);

    internal_midi_message_t message;
    memset(&message, 0, sizeof(message));
    size_t copy_len = length;
    if (copy_len > sizeof(message.data)) copy_len = sizeof(message.data);
    memcpy(message.data, data, copy_len);
    message.length = copy_len;

    // Tentar enviar para a fila
    BaseType_t queue_result = xQueueSend(global_driver_instance->tx_queue, &message, pdMS_TO_TICKS(100));

    if (queue_result != pdTRUE) {
        ESP_LOGE(DRIVER_TAG, "TX queue full or error");
        return false;
    }

    ESP_LOGI(DRIVER_TAG, "MIDI message queued for transmission");

    // Processamento imediato
    process_tx_queue(global_driver_instance);

    return true;
}
