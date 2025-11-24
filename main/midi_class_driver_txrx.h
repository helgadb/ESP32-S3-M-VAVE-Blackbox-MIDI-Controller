#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ===== API pública MIDI USB =====

// Indica se o driver está pronto para transmissão
bool midi_driver_ready_for_tx(void);

// Exibe status detalhado no log
void midi_driver_print_status(void);

// Envia dados MIDI brutos via USB
bool midi_send_data(const uint8_t *data, size_t length);

// Função que pode ser usada para processar dados USB e repassar para UART.
// Implementação disponível na biblioteca midi_uart; se ausente, é uma stub.
void process_usb_rx_for_uart(const uint8_t *data, size_t length);

// Estrutura de mensagem MIDI (usada para filas públicas)
typedef struct {
    uint8_t data[4];
    size_t length;
} midi_message_t;

// ===== Função da tarefa principal do driver =====
void class_driver_task(void *arg);

#ifdef __cplusplus
}
#endif
