#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <VL53L0X.h>
#include <LiquidCrystal_I2C.h>

// ── Настройки ─────────────────────────────────────────
#define WIFI_SSID   "TeamMehano"
#define WIFI_PASS   "45021510"
#define MQTT_HOST   "broker.hivemq.com"
#define MQTT_PORT   1883
#define MQTT_TOPIC  "archeoiot/abc123xyz"

#define DS18B20_PIN D5
#define EC_PIN      A0
#define MIXER_PIN   D6
#define SDA_PIN     4
#define SCL_PIN     5

const float EC_SLOPE        = 40.12f;
const unsigned long SEND_INTERVAL = 2000;
const unsigned long TEMP_CONV_MS  = 750;
const int           EC_SAMPLES    = 20;
const unsigned long EC_SAMPLE_MS  = 5;
const int           TANK_HEIGHT   = 1000; // мм — смени!

// ── Обекти ────────────────────────────────────────────
OneWire           oneWire(DS18B20_PIN);
DallasTemperature tempSensor(&oneWire);
LiquidCrystal_I2C lcd(0x27, 16, 2);
VL53L0X           lidar;
WiFiClient        espClient;
PubSubClient      mqtt(espClient);

bool lidarOK = false;

// ── Температура (неблокираща) ──────────────────────────
float         lastTemp        = 25.0f;
bool          tempRequested   = false;
unsigned long lastTempRequest = 0;

void handleTemp() {
  if (!tempRequested) {
    tempSensor.requestTemperatures();
    lastTempRequest = millis();
    tempRequested   = true;
    return;
  }
  if (millis() - lastTempRequest >= TEMP_CONV_MS) {
    float t = tempSensor.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C && t >= -10.0f) lastTemp = t;
    tempRequested = false;
  }
}

// ── EC + TDS семплиране (неблокиращо) ─────────────────
int           ecBuf[EC_SAMPLES];
int           ecBufIdx     = 0;
bool          ecReady      = false;
unsigned long lastEcSample = 0;
float         lastEC       = 0.0f;
float         lastTDS      = 0.0f;

void sampleEC() {
  if (millis() - lastEcSample >= EC_SAMPLE_MS) {
    lastEcSample      = millis();
    ecBuf[ecBufIdx++] = analogRead(EC_PIN);
    if (ecBufIdx >= EC_SAMPLES) {
      ecBufIdx = 0;
      ecReady  = true;
    }
  }
}

void calcECandTDS() {
  if (!ecReady) return;
  ecReady = false;

  long sum = 0;
  for (int i = 0; i < EC_SAMPLES; i++) sum += ecBuf[i];
  float voltage  = (sum / (float)EC_SAMPLES) * (3.3f / 1023.0f);
  float compCoef = 1.0f + 0.019f * (lastTemp - 25.0f);
  float vComp    = voltage / compCoef;
  lastEC  = (vComp > 0.05f) ? (EC_SLOPE * vComp) : 0.0f;
  lastTDS = voltage * 6.84f * 0.64f;
}

// ── Lidar ─────────────────────────────────────────────
int lastWL = 0;

void readLidar() {
  if (!lidarOK) return;
  uint16_t d = lidar.readRangeContinuousMillimeters();
  if (!lidar.timeoutOccurred()) {
    lastWL = constrain(map(d, TANK_HEIGHT, 0, 0, 100), 0, 100);
  }
}

// ── LCD ───────────────────────────────────────────────
void updateLCD() {
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(lastTemp, 1);
  lcd.print((char)223);
  lcd.print("C WL:");
  lcd.print(lastWL);
  lcd.print("%  ");

  lcd.setCursor(0, 1);
  lcd.print("EC:");
  lcd.print(lastEC, 1);
  lcd.print(" TDS:");
  lcd.print(lastTDS, 2);
  lcd.print("  ");
}

// ── MQTT (неблокиращо) ────────────────────────────────
unsigned long lastMqttAttempt = 0;

void handleMQTT() {
  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }
  if (millis() - lastMqttAttempt < 5000) return;
  lastMqttAttempt = millis();

  Serial.print("MQTT свързване...");
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("MQTT connecting");

  String clientId = "ESP8266_Archeo_" + String(ESP.getChipId());
  if (mqtt.connect(clientId.c_str())) {
    Serial.println(" OK!");
    lcd.setCursor(0, 1); lcd.print("MQTT OK!");
  } else {
    Serial.printf(" Грешка: %d\n", mqtt.state());
    lcd.setCursor(0, 1);
    lcd.print("Err:" + String(mqtt.state()));
  }
}

// ── Setup ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("Mehano booting..");

  pinMode(MIXER_PIN, OUTPUT);
  digitalWrite(MIXER_PIN, HIGH);

  tempSensor.begin();
  tempSensor.setWaitForConversion(false);

  lidar.setTimeout(200);
  lidarOK = lidar.init();
  if (lidarOK) {
    lidar.startContinuous();
    Serial.println("Lidar: OK");
  } else {
    Serial.println("Lidar: не е намерен");
  }

  // WiFi
  Serial.print("WiFi свързване");
  lcd.setCursor(0, 1); lcd.print("WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nСвързан! IP: " + WiFi.localIP().toString());
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WiFi OK!");
    lcd.setCursor(0, 1); lcd.print(WiFi.localIP().toString());
    delay(1500);
  } else {
    Serial.println("\nWiFi неуспешно - offline режим");
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WiFi FAIL");
    delay(1500);
  }

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(60);

  lcd.clear();
}

// ── Loop ──────────────────────────────────────────────
void loop() {
  handleMQTT();
  handleTemp();
  sampleEC();
  calcECandTDS();

  static unsigned long lastSend = 0;
  if (millis() - lastSend >= SEND_INTERVAL) {
    lastSend = millis();

    readLidar();

    Serial.printf("T: %.1f C | EC: %.2f | TDS: %.2f | WL: %d%%\n",
                  lastTemp, lastEC, lastTDS, lastWL);

    // Publish към MQTT
    if (mqtt.connected()) {
      StaticJsonDocument<128> doc;
      doc["temp"] = round(lastTemp * 10)  / 10.0;
      doc["ec"]   = round(lastEC   * 100) / 100.0;
      doc["tds"]  = round(lastTDS  * 100) / 100.0;
      doc["wl"]   = lastWL;

      String json;
      serializeJson(doc, json);

      if (mqtt.publish(MQTT_TOPIC, json.c_str()))
        Serial.println("✓ " + json);
      else
        Serial.println("✗ MQTT грешка");
    }

    updateLCD();
  }
}