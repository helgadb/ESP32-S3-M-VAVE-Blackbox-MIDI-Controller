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

// ===== Função da tarefa principal do driver =====
void class_driver_task(void *arg);

#ifdef __cplusplus
}
#endif
