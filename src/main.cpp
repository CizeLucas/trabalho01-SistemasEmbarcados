#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h> // Voltamos para a biblioteca correta
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include "freertos/semphr.h"

// --- CONFIGURAÇÕES DE HARDWARE ---
#define PINO_SENSOR 4
#define PIN_BOTAO   27  
#define I2C_ADDRESS 0x3C 
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1   

// --- ESTRUTURA DE DADOS ---
struct HistoricoData {
  float leituras[6];   
  int indiceAtual;     
};

#define TEMPERATURA_VAZIA -999.0 

HistoricoData historico; 
float temperaturaAtualSensor = 0.0; 

// --- OBJETOS GLOBAIS ---
// Instância para SH1106 (A que funcionava no seu primeiro código)
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

OneWire oneWire(PINO_SENSOR);
DallasTemperature sensors(&oneWire);

// --- FREERTOS ---
SemaphoreHandle_t xMutex;
TaskHandle_t taskInputHandle;

// ==========================================
// TAREFA DO CORE 0: Botão e EEPROM
// ==========================================
void TaskInput(void *pvParameters) {
  unsigned long lastDebounce = 0;

  for (;;) {
    if (digitalRead(PIN_BOTAO) == LOW) {
      if (millis() - lastDebounce > 500) { 
        lastDebounce = millis();

        Serial.println("Botão pressionado! Salvando...");

        if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
          historico.leituras[historico.indiceAtual] = temperaturaAtualSensor;
          historico.indiceAtual++;
          if (historico.indiceAtual >= 6) historico.indiceAtual = 0;

          EEPROM.put(0, historico);
          EEPROM.commit();
          
          int indiceGravado = (historico.indiceAtual == 0) ? 5 : historico.indiceAtual - 1;
          Serial.print("Salvo no indice: ");
          Serial.println(indiceGravado); 
          
          xSemaphoreGive(xMutex);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50)); 
  }
}

// ==========================================
// SETUP (CORE 1)
// ==========================================
void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_BOTAO, INPUT_PULLUP);
  Wire.begin(21, 22); // SDA, SCL
  
  // --- INICIALIZAÇÃO DO DISPLAY SH1106 ---
  // Usamos begin(I2C_ADDRESS, true) conforme seu código original
  if(!display.begin(I2C_ADDRESS, true)) { 
    Serial.println(F("Erro SH1106 - Verifique conexões"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("Iniciando...");
  display.display();
  delay(1000);

  // Sensor
  sensors.begin();
  sensors.setResolution(12);
  sensors.setWaitForConversion(false);

  // EEPROM
  if (!EEPROM.begin(64)) {
    Serial.println("Erro EEPROM");
  } else {
    EEPROM.get(0, historico);
    // Validação de integridade
    if (historico.indiceAtual < 0 || historico.indiceAtual > 5) {
      Serial.println("Resetando memória...");
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
// DESENHO (INTERFACE GRÁFICA)
// ==========================================
void desenharInterface(float tAtual, HistoricoData dados) {
  display.clearDisplay(); // Limpa o buffer anterior
  
  // IMPORTANTE: Redefinir a cor sempre
  display.setTextColor(SH110X_WHITE);

  // --- Cabeçalho ---
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Sensor:");
  
  display.setTextSize(2);
  display.setCursor(50, 0);
  display.print(tAtual, 1);
  
  display.setTextSize(1); 
  display.cp437(true); 
  display.write(248); // Símbolo Grau
  display.print("C"); 

  display.drawLine(0, 18, 128, 18, SH110X_WHITE); 

  // --- Lista ---
  display.setTextSize(1);
  int yBase = 24; 
  int alturaLinha = 12;

  for (int i = 0; i < 6; i++) {
    int x = (i < 3) ? 0 : 68;
    int y = yBase + ( (i % 3) * alturaLinha );

    display.setCursor(x, y);
    display.print(i + 1);
    display.print(":");

    if (dados.leituras[i] <= -900) { // Checagem de segurança para valor vazio
      display.print("--.--");
    } else {
      display.print(dados.leituras[i], 2); 
      
      int lastIndex = (dados.indiceAtual == 0) ? 5 : dados.indiceAtual - 1;
      if (i == lastIndex) {
        display.print("<");
      }
    }
  }

  display.display(); // Envia o buffer para a tela
}

// ==========================================
// LOOP (CORE 1)
// ==========================================
void loop() {
  static unsigned long lastRead = 0;
  
  // Leitura Sensor a cada 2s
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

  // Prepara dados para display
  float tDisplay = 0;
  HistoricoData hDisplay;

  // Snapshot seguro dos dados
  if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
    tDisplay = temperaturaAtualSensor;
    hDisplay = historico; 
    xSemaphoreGive(xMutex);
  }

  // Desenha
  desenharInterface(tDisplay, hDisplay);
  
  // Delay para estabilidade do Core 1
  vTaskDelay(pdMS_TO_TICKS(100));
}