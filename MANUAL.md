# Manual do Usuário — Controlador MIDI Blackbox (ESP32-S3)

Este manual descreve a operação, navegação e estrutura do firmware do controlador MIDI baseado em **ESP32-S3**, desenvolvido sob medida para controle de pedaleiras (especialmente a **M-VAVE Blackbox** com firmwares V21+). O sistema suporta operação nos modos **USB Host** e **USB Device** (com salvamento automático da última seleção na memória NVS), contando com display OLED SSD1306, 10 botões físicos de acionamento MIDI e 4 botões dedicados de navegação/edição.

---

## 1. Visão Geral do Hardware e Modos de Inicialização

O controlador possui dois modos de operação USB. O sistema **salva automaticamente** a última configuração utilizada na memória interna (NVS). Caso deseje alternar o modo, basta manter pressionado o botão correspondente no momento em que ligar o controlador:

* **Modo Device:** Mantenha o **Botão 1 (GPIO 6)** pressionado ao ligar o controlador (salva como padrão para os próximos boots).
* **Modo Host (Padrão para a Blackbox):** Mantenha o **Botão 6 (GPIO 17)** pressionado ao ligar o controlador (salva como padrão para os próximos boots).
* **Boot Normal:** Se nenhum desses botões for pressionado ao ligar, o controlador inicia no último modo salvo na memória.

---

## 2. Estrutura de Presets e Páginas

O sistema opera com **80 presets totais**, organizados em duas camadas lógicas:
1. **Páginas de Presets (Páginas 0 a 7):** Cada página contém 10 botões virtuais, totalizando os 80 presets do sistema (`01` a `80`).
2. **Páginas de Effect Change (Páginas 8 a 87):** Cada um dos 80 presets possui uma **página de efeitos dedicada** para controle individual de blocos (FX, AMP, MOD, Delay, Reverb), controles de Tap Tempo e navegação sequencial de presets.

---

## 3. Navegação e Funções por Pressionamento Longo (>500ms)

O comportamento de pressionamento longo (>500ms) funciona de forma unificada em **todas as páginas** (tanto nas páginas de Presets quanto nas páginas de Effect Change):

| Botão Físico | Ação de Pressionamento Longo (>500ms) |
| :--- | :--- |
| **Botão 1** | Vai direto para a **Página de Presets 0** (Presets 01–10) |
| **Botão 2** | Vai direto para a **Página de Presets 1** (Presets 11–20) |
| **Botão 3** | Vai direto para a **Página de Presets 2** (Presets 21–30) |
| **Botão 4** | Vai direto para a **Página de Presets 3** (Presets 31–40) |
| **Botão 5** | **Afinador (Tuner — Alternância Ligar/Desligar):** Funciona globalmente em qualquer página. O 1º toque longo envia `CC 28` com valor `127` (`{0x0B, 0xB0, 0x1C, 0x7F}`) para ativar o afinador; o 2º toque longo envia `CC 28` com valor `0` (`{0x0B, 0xB0, 0x1C, 0x00}`) para sair do afinador. |
| **Botão 6** | Vai direto para a **Página de Presets 4** (Presets 41–50) |
| **Botão 7** | Vai direto para a **Página de Presets 5** (Presets 51–60) |
| **Botão 8** | Vai direto para a **Página de Presets 6** (Presets 61–70) |
| **Botão 9** | Vai direto para a **Página de Presets 7** (Presets 71–80) |
| **Botão 10** | **Ação Contextual:**<br>• **Nas Páginas de Presets (0–7):** Entra na página de *Effect Change* do preset atualmente selecionado.<br>• **Nas Páginas de Effect Change (8–87):** Sai da tela de efeitos e retorna à página de presets de origem, mantendo o cursor sobre o preset exato. |

---

## 4. Páginas de Effect Change (Efeitos por Preset)

Ao acionar o Botão 10 por mais de 500ms em cima de um preset, o display exibe o identificador do preset (ex: `Ef:01` a `Ef:80`) e os toques curtos nos botões físicos assumem funções dedicadas ao controle de blocos de efeito, Tap Tempo e troca rápida de presets:

| Botão Físico | Função na Página de Efeitos (Toque Curto) | Comando MIDI Padrão (Hex / Decimal) |
| :--- | :--- | :--- |
| **Botão 1** | **FX** (Efeitos de drive/filtro/compressor) | `{0x0B, 0xB0, 0x00, 0x00}` (CC 0, Val 0) |
| **Botão 2** | **MOD** (Modulação) | `{0x0B, 0xB0, 0x02, 0x00}` (CC 2, Val 0) |
| **Botão 3** | **DLY** (Delay) | `{0x0B, 0xB0, 0x03, 0x00}` (CC 3, Val 0) |
| **Botão 4** | **TAP TEMPO MOD:** Define o andamento da modulação por batidas. | `{0x0B, 0xB0, 0x28, 0x7F}` (CC 40, Val 127) |
| **Botão 5** | **TAP TEMPO DLY:** Define o andamento do delay por batidas. | `{0x0B, 0xB0, 0x29, 0x7F}` (CC 41, Val 127) |
| **Botão 6** | **REV** (Reverb) | `{0x0B, 0xB0, 0x04, 0x00}` (CC 4, Val 0) |
| **Botão 7** | **AMP** (Amplificador) | `{0x0B, 0xB0, 0x01, 0x00}` (CC 1, Val 0) |
| **Botão 8** | **CAB** (Cabinet / IR) | `{0x0B, 0xB0, 0x05, 0x00}` (CC 5, Val 0) |
| **Botão 9** | **Preset Anterior:** Desce para a página de efeitos do preset imediatamente anterior e envia o comando MIDI do novo preset para a pedaleira. | *Navegação + MIDI* |
| **Botão 10** | **Preset Posterior:** Sobe para a página de efeitos do preset imediatamente posterior e envia o comando MIDI do novo preset para a pedaleira. | *Navegação + MIDI* |

---

## 5. Modo de Edição de Comandos MIDI

Para alterar qualquer comando MIDI enviado por um botão virtual (seja na página de presets ou de efeitos, incluindo os botões de Tap Tempo):

1. Selecione o botão desejado usando os botões de navegação físicos (`UP` / `DOWN`) ou pressionando rapidamente o botão físico correspondente.
2. Pressione o botão **Star (`*`)** para entrar no **`MODE_EDIT`**.
3. Utilize os botões **UP** e **DOWN** para alterar o valor do nibble atual (hexadecimal).
4. Pressione o botão **Star (`*`)** para alternar entre os bytes e nibbles do pacote MIDI de 4 bytes.
5. Pressione o botão **Hash (`#`)** para **salvar** na memória interna (NVS) e retornar ao modo normal.
6. Pressione e segure o botão **Hash (`#`)** por mais de 1 segundo para **cancelar** a edição sem salvar.

---

## 6. Display e Indicadores Visuais

* **Linha Superior (Modo Normal):** Exibe a porcentagem da bateria (`Bat:XXX%`) e a localização atual (`Pg:X/7` para páginas de preset de 0 a 7, ou `Ef:01` a `Ef:80` para as páginas de efeitos). Presets de 1 a 9 utilizam zero à esquerda (`Ef:01` a `Ef:09`) para evitar confusão visual com as páginas de preset.
* **Modo Standby (Economia de Energia):** Após inatividade, o display entra em modo de economia exibindo o número do preset (`01` a `80`) ou da página atual (`0` a `7`) em tamanho ampliado.