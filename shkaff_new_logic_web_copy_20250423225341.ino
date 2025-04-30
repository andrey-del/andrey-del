/*
 * Система управления микроклиматом с LCD дисплеем и веб-интерфейсом
 * Поддерживаемые компоненты: DHT22, LCD 16x2 I2C, реле, кнопки, светодиоды
 * Автор: OpenAI Assistant
 * Версия: 2.3 (с веб-интерфейсом)
 */

#include <DHT.h>              
#include <Wire.h>             
#include <hd44780.h>          
#include <hd44780ioClass/hd44780_I2Cexp.h> 
#include <EEPROM.h>           
#include <WiFi.h>
#include <WebServer.h>

// Конфигурационные константы
#define DHTPIN 23             
#define DHTTYPE DHT22         
#define DEBOUNCE_DELAY 50     
#define BACKLIGHT_TIMEOUT 3000 
#define HUMIDITY_HYSTERESIS 0.2

// Настройки WiFi
const char* ssid = "Keenetic-4075";
const char* password = "";
WebServer server(80);

// Назначение пинов микроконтроллера
const uint8_t buttonUPH = 16;    
const uint8_t buttonDOWNH = 17;  
const uint8_t doorPin = 18;      
const uint8_t redLED = 13;       
const uint8_t greenLED = 12;     
const uint8_t yellowLED = 15;    
const uint8_t fanPin = 32;       
const uint8_t flapPin = 33;      
const uint8_t flapPin1 = 25;     
const uint8_t HEATER_PIN = 26;   

// Структура для хранения состояния системы
struct SystemState {
    int humiditySetpoint = 0;  
    float humidity = 0;        
    
    bool heaterOn = false;     
    bool fanOn = false;        
    bool flapOn = false;       
    bool regeneration = false;
    bool dryingPhase = false; 
    
    bool doorOpen = false;        
    bool cycleRunning = false;    
    bool fanWaiting = false;      
    bool doorStateChanged = false;
    bool prevDoorOpen = false;
    float prevHumidity = 0;            // Предыдущее значение влажности
    unsigned long dehumidStartTime = 0; // Время начала отслеживания осушения
    unsigned long lastDebounceTime = 0;
    unsigned long dryingStartTime = 0;
    const uint8_t debounceDelay = 50;
    bool dehumidMonitoring = false;    // Флаг активности отслеживания    

} state;

hd44780_I2Cexp lcd(0x27);    
DHT dht(DHTPIN, DHTTYPE);     

// Переменные для управления временем
unsigned long lastButtonPress = 0;  
unsigned long fanStartTime = 0;     
unsigned long heatStartTime = 0;    
unsigned long flapStartTime = 0;    
unsigned long fanWaitStart = 0;
unsigned long lastWifiReconnect = 0;
unsigned long lastSensorCheck = 0; 
unsigned long fanStartTimeNormal = 0; 

void initPins() {
    const uint8_t pins[] = {
        HEATER_PIN, 
        redLED, greenLED, yellowLED, 
        fanPin, flapPin, flapPin1
    };
    for(uint8_t pin : pins) pinMode(pin, OUTPUT);
    
    const uint8_t inputPins[] = {doorPin, buttonUPH, buttonDOWNH};
    for(uint8_t pin : inputPins) pinMode(pin, INPUT_PULLUP);
}

String getSystemStatus() {
    String status = "";
    status += "Humidity: " + String(state.humidity, 1) + "%\n";
    status += "Setpoint: " + String(state.humiditySetpoint) + "%\n";
    status += "Fan: " + String(state.fanOn ? "ON" : "OFF") + "\n";
    status += "Heater: " + String(state.heaterOn ? "ON" : "OFF") + "\n";
    status += "Door: " + String(state.doorOpen ? "OPEN" : "CLOSED") + "\n";
    status += "Regen: " + String(state.regeneration ? "ACTIVE" : "INACTIVE");
    return status;
}
String SendHTML(float TempCstat, float Humiditystat) {
  String ptr = "<!DOCTYPE html><html lang='ru'>";
  ptr += "<head>";
  ptr += "<meta charset='UTF-8'>";
  ptr += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  ptr += "<meta http-equiv='refresh' content='10'>";
  ptr += "<title>Шкаф сухого хранения | Модель XT-2023</title>";
  ptr += "<style>";
  ptr += ":root {--color-alarm: #e74c3c; --color-warning: #f1c40f; --color-ok: #2ecc71;}";
  ptr += "body {font-family: 'Roboto', sans-serif; background: #f4f6f8; margin: 0; padding: 20px;}";
  ptr += ".container {max-width: 800px; margin: 0 auto; background: white; border-radius: 12px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); padding: 25px;}";
  ptr += ".status-bar {display: flex; gap: 15px; margin-bottom: 25px;}";
  ptr += ".status-item {flex: 1; padding: 15px; border-radius: 8px; color: white; text-align: center;}";
  ptr += ".main-panel {display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px;}";
  ptr += ".parameter-card {background: #f8f9fa; padding: 20px; border-radius: 8px; border-left: 4px solid;}";
  ptr += ".control-group {margin-top: 25px; border-top: 2px solid #eee; padding-top: 20px;}";
  ptr += "input[type='number'] {padding: 8px 12px; border: 1px solid #ddd; border-radius: 4px; width: 100px;}";
  ptr += "button {background: #3498db; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; transition: 0.3s;}";
  ptr += "button:hover {background: #2980b9;}";
  ptr += ".alarm {background: var(--color-alarm); border-color: var(--color-alarm);}";
  ptr += ".warning {background: var(--color-warning); border-color: var(--color-warning);}";
  ptr += ".normal {background: var(--color-ok); border-color: var(--color-ok);}";
  ptr += "h1 {color: #2c3e50; margin: 0 0 25px 0; font-weight: 500;}";
  ptr += "h2 {margin: 0 0 15px 0; font-size: 1.1em; color: #7f8c8d;}";
  ptr += ".value {font-size: 2.2em; font-weight: 300; margin: 10px 0;}";
  ptr += ".unit {font-size: 0.8em; color: #95a5a6;}";
  ptr += "</style></head>";

  ptr += "<body><div class='container'>";
  ptr += "<h1>Шкаф сухого хранения XT-2023</h1>";
  
  // Статусная панель
  ptr += "<div class='status-bar'>";
  ptr += "<div class='status-item ";
  ptr += (state.doorOpen ? "alarm" : (state.regeneration ? "warning" : "normal"));
  ptr += "'>";
  ptr += "<div>Режим работы</div>";
  ptr += "<div style='font-size:1.2em; margin-top:5px;'>";
  ptr += (state.doorOpen ? "Door Open" : (state.regeneration ? "Regeneration" : (state.fanOn ? "Fan Active" : "Standby")));
  ptr += "</div></div>";
  
  ptr += "<div class='status-item ";
  ptr += (state.doorOpen ? "alarm" : "normal");
  ptr += "'>";
  ptr += "<div>Состояние двери</div>";
  ptr += "<div style='font-size:1.2em; margin-top:5px;'>";
  ptr += (state.doorOpen ? "ОТКРЫТА" : "ЗАКРЫТА");
  ptr += "</div></div></div>";

  // Основные параметры
  ptr += "<div class='main-panel'>";
  ptr += "<div class='parameter-card'>";
  ptr += "<h2>ТЕКУЩАЯ ВЛАЖНОСТЬ</h2>";
  ptr += "<div class='value'>" + String(Humiditystat, 1) + "<span class='unit'>%</span></div>";
  ptr += "</div>";

  ptr += "<div class='parameter-card'>";
  ptr += "<h2>ТЕМПЕРАТУРА</h2>";
  ptr += "<div class='value'>" + String(TempCstat, 1) + "<span class='unit'>°C</span></div>";
  ptr += "</div></div>";

  // Управление
  ptr += "<div class='control-group'>";
  ptr += "<h2>УПРАВЛЕНИЕ УСТАВКАМИ</h2>";
  ptr += "<form action='/set' method='POST' style='display:flex; gap:15px; align-items:center;'>";
  ptr += "<div style='flex:1;'>";
  ptr += "<label>Уставка влажности (%):</label>";
  ptr += "<input type='number' name='sp' value='" + String(state.humiditySetpoint) + "' min='5' max='50' step='1'>";
  ptr += "</div>";
  ptr += "<button type='submit'>ПРИМЕНИТЬ</button>";
  ptr += "</form></div>";

  // Системная информация
  ptr += "<div class='parameter-card' style='margin-top:20px;'>";
  ptr += "<h2>СИСТЕМНАЯ ИНФОРМАЦИЯ</h2>";
  ptr += "<div style='display:grid; grid-template-columns: repeat(2, 1fr); gap:10px;'>";
  ptr += "<div>IP адрес: " + WiFi.localIP().toString() + "</div>";
  ptr += "<div>Версия ПО: 2.7</div>";
  ptr += "<div>Время работы: " + String(millis()/3600000) + " ч</div>";
  ptr += "</div></div>";

  ptr += "</div></body></html>";
  return ptr;
}

void handleRoot() {
    String html = SendHTML(dht.readTemperature(), dht.readHumidity());
    server.send(200, "text/html", html);
}

void handleSet() {
    if (server.hasArg("sp")) {
        int newSetpoint = server.arg("sp").toInt();
        if (newSetpoint != state.humiditySetpoint) { // Проверка изменения
            state.humiditySetpoint = newSetpoint;
            EEPROM.put(0, state.humiditySetpoint);
            EEPROM.commit();
        }
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void setup() {
    Serial.begin(115200);      
    Wire.begin();              
    
    lcd.begin(16, 2);
    lcd.print("Initializing...");
    
    dht.begin();               
    EEPROM.begin(64);          
    EEPROM.get(0, state.humiditySetpoint); 
    
    initPins();                
    delay(1000);              
    lcd.clear();              

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nIP: " + WiFi.localIP().toString());

    server.on("/", handleRoot);
    server.on("/set", HTTP_POST, []() {
        if (server.hasArg("sp")) {
            state.humiditySetpoint = server.arg("sp").toInt();
            EEPROM.put(0, state.humiditySetpoint);
            EEPROM.commit();
        }
        server.sendHeader("Location", "/");
        server.send(303);
    });
    server.onNotFound([]() {
        server.send(404, "text/plain", "Not Found");
    });
    
    server.begin();
}

void updateDisplay() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("H:");
    lcd.print(state.humidity, 1);
    lcd.print("% (");
    lcd.print(state.humiditySetpoint);
    lcd.print("%)");
    
    lcd.setCursor(0, 1);
    if(state.doorOpen) {
        lcd.print("DOOR OPEN!");
        lcd.backlight(); 
    }
    else if(state.regeneration) {
        const unsigned long HEAT_DURATION = 3600000;    // 60 минут
        const unsigned long COOLING_DURATION = 1500000;  // 20 минут
        unsigned long remaining = 0;

        if (state.heaterOn) {
            remaining = HEAT_DURATION - (millis() - heatStartTime);
            if (remaining > HEAT_DURATION) remaining = 0; // Защита от переполнения
        } 
        else {
            remaining = COOLING_DURATION - (millis() - flapStartTime);
            if (remaining > COOLING_DURATION) remaining = 0;
        }

        // Форматирование времени
        uint8_t minutes = remaining / 60000;
        uint8_t seconds = (remaining % 60000) / 1000;

        lcd.print("REGEN ");
        printTwoDigits(minutes);
        lcd.print(":");
        printTwoDigits(seconds);
    }
    else if(state.fanOn) {
        lcd.print("FAN ACTIVE");
    }
    else {
        lcd.print("NORMAL MODE");
    }
}

// Вспомогательная функция для форматирования
void printTwoDigits(uint8_t number) {
    if(number < 10) lcd.print('0');
    lcd.print(number);
}

void controlPeripherals() {
    digitalWrite(redLED, state.doorOpen);
    digitalWrite(greenLED, !state.doorOpen);
    digitalWrite(yellowLED, state.regeneration);
    
    digitalWrite(fanPin, state.fanOn);
    digitalWrite(HEATER_PIN, state.heaterOn);
    digitalWrite(flapPin, state.flapOn);
    digitalWrite(flapPin1, state.flapOn);
}

void checkDoorStateChange() {
    bool currentState = digitalRead(doorPin);
    if (currentState != state.prevDoorOpen && millis() - state.lastDebounceTime > state.debounceDelay) {
        state.doorStateChanged = true;
        state.prevDoorOpen = currentState;
        state.lastDebounceTime = millis();
    }
}

void handleDoorStateChange() {
    if (!state.doorStateChanged) return;

    if (state.doorOpen) { // Если дверь открыта
        state.fanOn = false;
        state.heaterOn = false;
        state.flapOn = false;
        state.regeneration = false;
        state.cycleRunning = false;
        heatStartTime = 0;    // Сброс метки нагрева
        flapStartTime = 0;    // Сброс метки заслонок
        state.dehumidStartTime = 0;
    } else {
        state.humidity = dht.readHumidity();
    }
    state.doorStateChanged = false;
}

void handleNormalMode() {
    if (state.doorOpen || state.regeneration) {
//        state.fanOn = false;
        return;
    }

    // Гистерезис с двумя границами
    if (state.humidity > (state.humiditySetpoint + HUMIDITY_HYSTERESIS)) {
        state.fanOn = true;  // Включение при превышении верхней границы
    } 
    else if (state.humidity < (state.humiditySetpoint - HUMIDITY_HYSTERESIS)) {
        state.fanOn = false; // Выключение при снижении ниже нижней границы
    }
    // Между границами - сохраняем текущее состояние

    // Сброс связанных флагов
    fanStartTimeNormal = 0;
    state.fanWaiting = false;
    
    if (state.dryingPhase) {
        state.dryingPhase = false;
        state.cycleRunning = false;
    }
}

void handleButtons() {
    static unsigned long lastIncrementTime = 0;
    static unsigned long lastDecrementTime = 0;
    const unsigned long initialDelay = 500;  
    const unsigned long fastDelay = 100;     
    
    bool upPressed = digitalRead(buttonUPH) == LOW;
    bool downPressed = digitalRead(buttonDOWNH) == LOW;
    unsigned long currentTime = millis();

    if(upPressed) {
        if(lastIncrementTime == 0 || 
          (currentTime - lastIncrementTime > (currentTime - lastButtonPress > initialDelay ? fastDelay : initialDelay))) {
            state.humiditySetpoint++;
            EEPROM.put(0, state.humiditySetpoint);
            EEPROM.commit();
            lastIncrementTime = currentTime;
            lcd.backlight();
            lastButtonPress = currentTime;
        }
    } else lastIncrementTime = 0;

    if(downPressed) {
        if(lastDecrementTime == 0 || 
          (currentTime - lastDecrementTime > (currentTime - lastButtonPress > initialDelay ? fastDelay : initialDelay))) {
            state.humiditySetpoint--;
            EEPROM.put(0, state.humiditySetpoint);
            EEPROM.commit();
            lastDecrementTime = currentTime;
            lcd.backlight();
            lastButtonPress = currentTime;
        }
    } else lastDecrementTime = 0;

    static int lastSetpoint = -1;
    if(state.humiditySetpoint != lastSetpoint) {
        updateDisplay();
        lastSetpoint = state.humiditySetpoint;
    }
}

void handleRegeneration() {
    const float DEHUMID_RATE_THRESHOLD = 0.01;        // 0.01% за 60 минут
    const unsigned long DEHUMID_CHECK_TIME = 3600000; // 60 минут
    const unsigned long HEAT_DURATION = 3600000;     // 60 минут
    const unsigned long COOLING_DURATION = 1500000;  // 25 минут

    if (state.doorOpen) return;

    // Проверка скорости осушения
    if (!state.dehumidMonitoring && state.fanOn) {
        state.prevHumidity = state.humidity;
        state.dehumidStartTime = millis();
        state.dehumidMonitoring = true;
    }

    // Активация регенерации при низкой скорости осушения
    if (state.dehumidMonitoring && (millis() - state.dehumidStartTime >= DEHUMID_CHECK_TIME)) {
        float humidityDiff = state.prevHumidity - state.humidity;
        float dehumidRate = humidityDiff / (DEHUMID_CHECK_TIME / 60000.0);

        if (dehumidRate < DEHUMID_RATE_THRESHOLD && !state.regeneration) {
            state.regeneration = true;
            state.heaterOn = true;
            state.fanOn = false; // Выключение вентилятора при старте регенерации
            heatStartTime = millis();
        }
        state.dehumidMonitoring = false;
    }

    // Управление фазами регенерации
    if (state.regeneration) {
        // Фаза нагрева (60 минут)
        if (state.heaterOn) {
            if (millis() - heatStartTime >= HEAT_DURATION) {
                state.heaterOn = false;
                state.flapOn = true;
                state.fanOn = true; // Включение вентилятора на фазе охлаждения
                flapStartTime = millis();
            }
        }
        // Фаза охлаждения (25 минут)
        else if (millis() - flapStartTime >= COOLING_DURATION) {
            state.regeneration = false;
            state.flapOn = false;
            state.fanOn = false; // Выключение вентилятора после завершения

            // Проверка условий для возврата в режимы
            state.humidity = dht.readHumidity();
            if (state.humidity > state.humiditySetpoint + HUMIDITY_HYSTERESIS) {
                state.fanOn = true; // Осушение
            }
        }
    }
}

void loop() {
    static unsigned long lastUpdate = 0;
    
    state.doorOpen = digitalRead(doorPin);
    checkDoorStateChange();
    
    server.handleClient();

    if (WiFi.status() != WL_CONNECTED && millis() - lastWifiReconnect > 10000) {
        WiFi.reconnect();
        lastWifiReconnect = millis();
    }    
    
    if(millis() - lastUpdate >= 1000) {
        lastUpdate = millis();
        
        handleDoorStateChange();
        state.humidity = dht.readHumidity();
        if (isnan(state.humidity)) {
            dht.begin(); // Переинициализация
            delay(200);
            return; // Пропустить цикл при ошибке
        }
        
        // checkLowHumidityConditions();
        handleButtons();
        handleNormalMode();
        handleRegeneration();
        updateDisplay();
        controlPeripherals();
        
        if(!state.doorOpen && (millis() - lastButtonPress >= BACKLIGHT_TIMEOUT)) {
            lcd.noBacklight();
        }
    }
    
    if(state.doorOpen) {
        lcd.backlight();
        state.fanOn = false;
        state.heaterOn = false;
        state.flapOn = false;
        state.regeneration = false;
        state.dryingPhase = false;
        controlPeripherals();
        heatStartTime = 0;    // Сброс метки нагрева
        flapStartTime = 0;     // Сброс метки заслонок
        state.dehumidStartTime = 0; // Сброс отслеживания осушения
    }
}

