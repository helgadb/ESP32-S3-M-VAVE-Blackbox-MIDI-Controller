Circuito de Monitoramento de Bateria:

GPIO2 = HIGH → Transistor NPN liga → MOSFET liga → Tensão flui para o divisor
                    ↓
            ADC lê a tensão no GPIO1
                    ↓
            Calcula a porcentagem da bateria (0% a 100%)
                    ↓
GPIO2 = LOW → NPN desliga → MOSFET desliga → Consumo zero

Componente:                                   Função:
IRF9610 (MOSFET P-Channel)	                  Atua como chave eletrônica, conectando/desconectando a bateria do divisor de tensão
SD965 (Transistor NPN)	                      Driver de acionamento do MOSFET. Converte o sinal de 3.3V do ESP32 para controlar a tensão da bateria (4.2V). 
2x Resistores de 100kΩ (Divisor de tensão)	  Reduz a tensão da bateria pela metade (máx. 4.2V → 2.1V), adequando-a ao limite do ADC do ESP32
GPIO2 (ESP32-S3)	Pino de ativação            Emite sinal HIGH por 50ms durante a medição
GPIO1 / ADC1_CH0 (ESP32-S3)	Pino de leitura   Lê a tensão dividida para calcular a porcentagem da bateria
