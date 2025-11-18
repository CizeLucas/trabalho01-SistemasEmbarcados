# 🌡️ Termômetro Digital com Persistência de Dados (ESP32)

> Trabalho apresentado para a disciplina de **Sistemas Embarcados**.

Este projeto implementa um sistema de monitoramento de temperatura capaz de salvar medições na memória não-volátil (EEPROM/Flash) do ESP32. O sistema utiliza **FreeRTOS** para dividir o processamento entre os dois núcleos do microcontrolador, garantindo alta responsividade da interface e estabilidade na leitura dos sensores.

## 🚀 Funcionalidades

* **Leitura em Tempo Real:** Exibição da temperatura via sensor DS18B20.
* **Persistência de Dados:** Armazenamento de histórico na memória Flash (emulação EEPROM). Os dados não são perdidos ao desligar a energia.
* **Buffer Circular:** Armazena as últimas 6 medições. Ao encher, substitui automaticamente a mais antiga.
* **Multitarefa (Dual Core):**
    * **Core 0:** Processamento de inputs (Botões) e gravação na memória (tarefas críticas).
    * **Core 1:** Leitura do sensor e atualização do display OLED (tarefas de interface).
* **Controles Físicos:** Botão para salvar medição e botão para resetar o histórico.

## 🛠️ Hardware Utilizado

* **MCU:** ESP32 (DevKit V1)
* **Display:** OLED 1.3" I2C (Driver SH1106)
* **Sensor:** DS18B20 (Prova d'água)
* **Entradas:** 2x Push Buttons (Salvar e Reset)
* **Resistores:** 1x 4.7kΩ (Pull-up do sensor), resistores internos de pull-up utilizados para os botões.

## 🔌 Pinagem (Pinout)

| Componente | Pino ESP32 | Função |
| :--- | :--- | :--- |
| **OLED SDA** | GPIO 21 | Dados I2C |
| **OLED SCL** | GPIO 22 | Clock I2C |
| **Sensor DS18B20** | GPIO 4 | Dados (OneWire) |
| **Botão Salvar** | GPIO 27 | Gravar na EEPROM |
| **Botão Reset** | GPIO 14 | Limpar Memória |

## 📚 Dependências (Bibliotecas)

Certifique-se de instalar as seguintes bibliotecas na IDE do Arduino ou PlatformIO:

1.  `Adafruit SH110X` (Display)
2.  `Adafruit GFX` (Gráficos)
3.  `DallasTemperature` (Sensor)
4.  `OneWire` (Protocolo Sensor)
5.  `EEPROM` (Nativa do Core ESP32)

## 🧠 Estrutura do Código

O projeto utiliza **Semáforos (Mutex)** para gerenciar o acesso à memória compartilhada entre os núcleos:

* `TaskInput` (Core 0): Monitora os botões com *debounce*. Se acionado, toma o Mutex, atualiza a `struct` de dados e realiza o `EEPROM.commit()`.
* `loop` (Core 1): Lê o sensor assincronamente e atualiza a interface gráfica (UI) no display SH1106.

## 📸 Layout do Display

```text
+------------------------+
| Sensor:      24.56°C   |  <-- Tempo Real
| ---------------------- |
| 1: 24.00     4: --.--  |  <-- Histórico
| 2: 25.10 <   5: --.--  |  <-- '<' indica último salvo
| 3: 23.80     6: --.--  |
+------------------------+