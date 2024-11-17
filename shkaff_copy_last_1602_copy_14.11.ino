#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEManager.h>
#define DHTPIN 23      // Пин, к которому подключен датчик влажности
#define DHTTYPE DHT22  // Используемый тип датчика DHT22
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHTPIN, DHTTYPE);

// Переопределение пинов
int buttonUPH = 16;    // Пин кнопки + влажности
int buttonDOWNH = 17;  // Пин кнопки - влажности

const int doorPin = 18;     // Пин датчика дверцы
const int redLED = 13;      // Красный светодиод
const int greenLED = 12;    // Зеленый светодиод
const int yellowLED = 15;   // Желтый светодиод
const int fanPin = 32;      // Пин управления вентилятором
const int flapPin = 33;     // Пин управления заслонками
const int flapPin1 = 25;    // Пин управления заслонками
const int HEATER_PIN = 26;  // Пин подключения нагревательного элемента
const int GasPin = 27;      // Пин подключения клапана азота
const int PinPin = 14;

// Переменные
int address = 0; // адрес памяти для записи (от 0 до 511)
int lastRH = 0;
int humiditySetpoint = 0;      // Заданное значение влажности
float humidityTolerance = 1.0;  // Допустимое отклонение влажности
float humidity = 0.0;
bool heaterOn = false;
bool cycleRunning = false;
bool fanOn = false;
bool flapOn = false;
bool regeneratiOn = false;
bool dehumi = false;
bool doorOpen = false;
bool isWaiting = false;

// Начальное значение таймеров
unsigned long previousFanMillis = 0;
unsigned long previousFanRMillis = 0;
unsigned long previousHeatRMillis = 0;
unsigned long previousGasMillis = 0;
unsigned long fanTimer = 0;
unsigned long backlightOffTime = 0;

// Таймеры
const int ON_TIME_H = 1200000;  // Время включения вентилятора в миллисекундах (20 минут)
const int interval = 300000;    // Время ожидания вентилятора в миллисекундах (5 минут)
const int interval2 = 3000000;  // Время нагрева в режиме регенерации в миллисекундах (50 минут)
const int interval3 = 1500000;  // Время работы вентилятора в режиме регенерации в миллисекундах (25 минут)
const int interval4 = 30000;    // Время открытия клапана азота*


void setup() {
  lcd.init();
  lcd.clear();
  lcd.backlight();
  pinMode(HEATER_PIN, OUTPUT);
  pinMode(GasPin, OUTPUT);
  pinMode(PinPin, OUTPUT);
  pinMode(doorPin, INPUT_PULLUP);
  pinMode(redLED, OUTPUT);
  pinMode(greenLED, OUTPUT);
  pinMode(yellowLED, OUTPUT);
  pinMode(fanPin, OUTPUT);
  pinMode(flapPin, OUTPUT);
  pinMode(flapPin1, OUTPUT);
  pinMode(buttonUPH, INPUT_PULLUP);
  pinMode(buttonDOWNH, INPUT_PULLUP);

  digitalWrite(redLED, LOW);
  digitalWrite(greenLED, LOW);
  digitalWrite(yellowLED, LOW);
  digitalWrite(fanPin, LOW);
  digitalWrite(flapPin, LOW);
  digitalWrite(flapPin1, LOW);
  digitalWrite(HEATER_PIN, LOW);
  digitalWrite(GasPin, LOW);
  digitalWrite(PinPin, LOW);

  Serial.begin(9600);
  EEPROM.begin(100);
  dht.begin();
  EEPROM.get(0, humiditySetpoint);
}
// Цикл
void loop() {
  unsigned long currentFanMillis = millis();
  unsigned long currentFanRMillis = millis();
  unsigned long currentHeatMillis = millis();
  unsigned long currentHeatRMillis = millis();
  unsigned long currentGasMillis = millis();
  int doorState = digitalRead(doorPin);
  float humidity = dht.readHumidity();
  lastRH = humidity;

  if (doorState == HIGH) {          // Дверца открыта
    digitalWrite(redLED, HIGH);     // Включаем красный светодиод
    digitalWrite(greenLED, LOW);    // Выключаем зеленый светодиод
    digitalWrite(yellowLED, LOW);   // Выключаем зеленый светодиод
    digitalWrite(fanPin, LOW);      // Выключаем вентиляторы
    digitalWrite(HEATER_PIN, LOW);  // Выключаем нагрев
    digitalWrite(flapPin, LOW);     // Закрываем заслонки
    digitalWrite(flapPin1, LOW);    // Закрываем заслонки
    lcd.backlight();
    cycleRunning = false;
    isWaiting = false;             // Сброс таймера
    heaterOn = false;              // Нагрев выключен
    regeneratiOn = false;          // Режим регенерации выключен
    flapOn = false;                // Заслонки закрыты
    doorOpen = true;               // Дверь открыта
  } else {                         // Дверца закрыта
    digitalWrite(redLED, LOW);     // Выключаем красный светодиод
    digitalWrite(greenLED, HIGH);  // Включаем зеленый светодиод
    doorOpen = false;              // Дверь закрыта

    if (!isWaiting && humidity > humiditySetpoint - humidityTolerance && !regeneratiOn) {  // Если таймер не запущен и влажность выше установленной (с учетом отклонения), включаем вентиляторы
      digitalWrite(fanPin, HIGH);                                                          // Включаем вентиляторы
      fanTimer = millis();                                                                 // Запускаем таймер
      isWaiting = true;                                                                    // Таймер запущен
      fanOn = true;                                                                        // Вентиляторы включены
      dehumi = true;

    } else if (humidity <= humiditySetpoint - humidityTolerance && !regeneratiOn) {  // Если таймер запущен и время прошло больше чем время включения вентилятора (20 мин.), выключаем вентиляторы
      digitalWrite(fanPin, LOW);                                                     // Выключаем вентиляторы
      fanOn = false;
      isWaiting = false;

    } else if (isWaiting && (millis() - fanTimer > ON_TIME_H)) {  // Если таймер запущен и время прошло больше чем время включения вентилятора (20 мин.), выключаем вентиляторы
      digitalWrite(fanPin, LOW);                                  // Выключаем вентиляторы
      fanOn = false;                                              // Вентиляторы выключены
    }
    if (currentFanMillis - previousFanMillis >= interval) {  // Запуск таймера ожидания, сравнение мс с интервалом
      previousFanMillis = currentFanMillis;
      if (!fanOn && isWaiting) {  // Если вентиляторы выключены и активно ожидание, выключаем ожидание
        isWaiting = false;        // Ожидание выключено
        previousFanMillis = millis();
      }
    }
  //}

  //Регенерация
    if (!cycleRunning && (lastRH + 1) > humiditySetpoint + 10) {  // Если цикл регенерации не включен и влажность установленная выше, чем текущая влажность +10%
      cycleRunning = true;                                        // Включаем цикл
      previousHeatRMillis = millis();
      digitalWrite(fanPin, LOW);
      fanOn = false;
      isWaiting = false;
      digitalWrite(yellowLED, HIGH);   // Включаем желтый светодиод
      regeneratiOn = true;             // Режим регенерации запущен
      digitalWrite(HEATER_PIN, HIGH);  // Включаем нагрев
      heaterOn = true;                 // Нагрев включен
    }
    if (currentHeatRMillis - previousHeatRMillis >= interval2) {  // Сравнение времени таймера с интервалом
      previousHeatRMillis = currentHeatRMillis;
      if (heaterOn && regeneratiOn) {  // Если нагрев включен и регенерация включена
        previousFanRMillis = millis();
        digitalWrite(HEATER_PIN, LOW);  // Выключаем нагрев
        heaterOn = false;               // Нагрев выключен
        digitalWrite(fanPin, HIGH);     // Включаем вентиляторы
        fanOn = true;                   // Вентиляторы включены
        digitalWrite(flapPin, HIGH);
        digitalWrite(flapPin1, HIGH);
        flapOn = true;
      }
      if (currentFanRMillis - previousFanRMillis >= interval3) {  // Сравнение времени таймера с интервалом
        previousFanRMillis = currentFanRMillis;
        if (fanOn || flapOn && regeneratiOn) {  // Если вентиляторы включены и регенерация включена
          digitalWrite(fanPin, LOW);            // Выключаем вентиляторы
          fanOn = false;                        // Вентиляторы выключены
          digitalWrite(flapPin, LOW);
          digitalWrite(flapPin1, LOW);
          flapOn = false;
          digitalWrite(yellowLED, LOW);
          regeneratiOn = false;
        }
      //      if (currentGasMillis - previousGasMillis >= interval4) {  // Сравнение времени таймера с интервалом
      //        previousGasMillis = currentGasMillis;
      //       if (!dehumi || !regeneratiOn) {
      //          digitalWrite(GasPin, HIGH);
      //          lcd.setCursor(3, 0);
      //          lcd.print("Gas");
      //          digitalWrite(GasPin, LOW);
      //        }
        cycleRunning = false;  // Т.к. цикл выполнен, выключаем цикл, для дальнейшего запуска
       }
     }
   }

  //Кнопки
  if (digitalRead(buttonUPH) == LOW) {  // Проверяем, нажат ли контакт
    humiditySetpoint++;                 // Увеличиваем установку влажности
    EEPROM.put(0, humiditySetpoint);
    EEPROM.commit(); 
  }
  if (digitalRead(buttonDOWNH) == LOW) {  // Проверяем, нажат ли контакт
    humiditySetpoint--;                   // Уменьшаем установку влажности
    EEPROM.put(0, humiditySetpoint);
    EEPROM.commit(); 
  }

  // Дисплей
  if (!digitalRead(buttonDOWNH) || !digitalRead(buttonUPH) || digitalRead(doorPin)) {
    lcd.backlight();
    backlightOffTime = millis() + 3000;  // Подсветка 3 секунды
  }
  if (millis() >= backlightOffTime) {
    lcd.noBacklight();
  }
  if (isnan(humidity)) {
    lcd.setCursor(0, 0);
    lcd.print("Failed");
  } else {
    lcd.setCursor(6, 0);
    lcd.print("Cur: ");
    lcd.setCursor(12, 0);
    lcd.print("Set: ");
    lcd.setCursor(0, 1);  // Позиция курсора
    lcd.print("Humi: ");
    lcd.setCursor(6, 1);
    lcd.print(lastRH);  // Влажность
    lcd.print("%");
    lcd.setCursor(12, 1);
    lcd.print(humiditySetpoint);
//    lcd.setCursor(15, 1);
//    lcd.print(cycleRunning);
  }
  delay(500);  // Задержка
}
