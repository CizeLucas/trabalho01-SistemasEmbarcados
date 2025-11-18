#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include "freertos/semphr.h"

// --- CONFIGURAÇÕES DE HARDWARE ---
#define PINO_SENSOR     4
#define PIN_BOTAO_SAVE  27  // Botão para guardar medida (GND)
#define PIN_BOTAO_RESET 14  // NOVO: Botão para limpar memória (GND)
#define I2C_ADDRESS     0x3C 
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1   

// --- ESTRUTURA DE DADOS ---
struct HistoricoData {
  float leituras[6];   
  int indiceAtual;     
};

#define TEMPERATURA_VAZIA -999.0 

HistoricoData historico; 
float temperaturaAtualSensor = 0.0; 

// --- OBJETOS GLOBAIS ---
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
OneWire oneWire(PINO_SENSOR);
DallasTemperature sensors(&oneWire);

// --- FREERTOS ---
SemaphoreHandle_t xMutex;
TaskHandle_t taskInputHandle;

// ==========================================
// TAREFA DO CORE 0: Gestão dos Botões e EEPROM
// ==========================================
void TaskInput(void *pvParameters) {
  unsigned long lastDebounceSave = 0;
  unsigned long lastDebounceReset = 0;

  for (;;) {
    
    // --- 1. LÓGICA BOTÃO SALVAR (GPIO 27) ---
    if (digitalRead(PIN_BOTAO_SAVE) == LOW) {
      if (millis() - lastDebounceSave > 500) { 
        lastDebounceSave = millis();
        Serial.println("Botão Save: Guardando...");

        if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
          // Guarda temperatura atual no slot da vez
          historico.leituras[historico.indiceAtual] = temperaturaAtualSensor;
          
          // Avança índice (Circular)
          historico.indiceAtual++;
          if (historico.indiceAtual >= 6) historico.indiceAtual = 0;

          // Grava na Flash
          EEPROM.put(0, historico);
          EEPROM.commit();
          
          xSemaphoreGive(xMutex);
        }
      }
    }

    // --- 2. LÓGICA BOTÃO RESET (GPIO 14) ---
    if (digitalRead(PIN_BOTAO_RESET) == LOW) {
      if (millis() - lastDebounceReset > 500) { 
        lastDebounceReset = millis();
        Serial.println("Botão Reset: Limpando memória...");

        if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
          // Loop para limpar todas as posições
          for(int i=0; i<6; i++) {
            historico.leituras[i] = TEMPERATURA_VAZIA;
          }
          historico.indiceAtual = 0; // Reinicia o ponteiro

          // Grava a memória limpa na Flash
          EEPROM.put(0, historico);
          EEPROM.commit();
          
          xSemaphoreGive(xMutex);
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50)); // Pequeno delay para não bloquear a CPU
  }
}

// ==========================================
// SETUP (CORE 1)
// ==========================================
void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_BOTAO_SAVE, INPUT_PULLUP);
  pinMode(PIN_BOTAO_RESET, INPUT_PULLUP); // Configura novo botão
  
  Wire.begin(21, 22);
  
  if(!display.begin(I2C_ADDRESS, true)) { 
    Serial.println(F("Erro SH1106"));
    for(;;);
  }
  
  // Efeito visual de inicialização
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(10,25);
  display.println("Sistema Termometro");
  display.display();
  delay(1000);

  sensors.begin();
  sensors.setResolution(12);
  sensors.setWaitForConversion(false);

  if (!EEPROM.begin(64)) {
    Serial.println("Erro EEPROM");
  } else {
    EEPROM.get(0, historico);
    // Validação
    if (historico.indiceAtual < 0 || historico.indiceAtual > 5) {
      historico.indiceAtual = 0;
      for(int i=0; i<6; i++) historico.leituras[i] = TEMPERATURA_VAZIA;
      EEPROM.put(0, historico);
      EEPROM.commit();
    }
  }

  xMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(TaskInput, "InputTask", 4096, NULL, 1, &taskInputHandle, 0);
}

// ==========================================
// DESENHO DA INTERFACE
// ==========================================
void desenharInterface(float tAtual, HistoricoData dados) {
  display.clearDisplay(); 
  display.setTextColor(SH110X_WHITE);

  // --- CABEÇALHO (Temperatura Principal) ---
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Atual:");
  
  display.setTextSize(2);
  display.setCursor(50, 0);
  // MUDANÇA: '2' casas decimais
  display.print(tAtual, 2);
  
  display.setTextSize(1); 
  display.cp437(true); 
  display.write(248); 
  display.print("C"); 

  display.drawLine(0, 18, 128, 18, SH110X_WHITE); 

  // --- LISTA DE MEDIÇÕES ---
  display.setTextSize(1);
  int yBase = 24; 
  int alturaLinha = 12;

  for (int i = 0; i < 6; i++) {
    int x = (i < 3) ? 0 : 68;
    int y = yBase + ( (i % 3) * alturaLinha );

    display.setCursor(x, y);
    display.print(i + 1);
    display.print(":");

    if (dados.leituras[i] <= -900) { 
      display.print("--.--");
    } else {
      display.print(dados.leituras[i], 2); 
      
      // Marcador da última gravação
      int lastIndex = (dados.indiceAtual == 0) ? 5 : dados.indiceAtual - 1;
      if (i == lastIndex) {
        display.print("<");
      }
    }
  }

  display.display();
}

// ==========================================
// LOOP PRINCIPAL (CORE 1)
// ==========================================
void loop() {
  static unsigned long lastRead = 0;
  
  // Leitura Sensor
  if (millis() - lastRead >= 2000) {
    lastRead = millis();
    float t = sensors.getTempCByIndex(0);
    sensors.requestTemperatures();
    
    if (t > -100.0) {
      if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        temperaturaAtualSensor = t;
        xSemaphoreGive(xMutex);
      }
    }
  }

  // Preparação para Display
  float tDisplay = 0;
  HistoricoData hDisplay;

  if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
    tDisplay = temperaturaAtualSensor;
    hDisplay = historico; 
    xSemaphoreGive(xMutex);
  }

  desenharInterface(tDisplay, hDisplay);
  vTaskDelay(pdMS_TO_TICKS(100));
}