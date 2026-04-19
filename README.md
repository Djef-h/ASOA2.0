# ASOA2.0
WebSocket е по-надежден за твоя случай, ето защо:
WebSocket ✅

Постоянна връзка — ESP праща данни веднага щом се променят
Много по-малко трафик (без повтарящи се HTTP headers)
Latency ~5ms срещу ~100-300ms при polling
ESP8266 има добра библиотека — WebSocketsServer от Markus Sattler
Веднага разбираш ако връзката падне (onclose event)

HTTP Polling ❌ проблеми

Всеки 2 секунди нов TCP handshake — товари ESP-то
ESP8266 има лимит ~4-5 едновременни TCP връзки, лесно се запушва
Ако заявката се забави, данните изглеждат "замръзнали"
Пропускаш бързи промени между двата polls


Практически съвет за теб:
Тъй като вече имаш Protocol: WebSocket в sys-info на проекта, ето минималния код за ESP-то:
cpp#include <WebSocketsServer.h>
#include <ArduinoJson.h>

WebSocketsServer webSocket(81);

void setup() {
  webSocket.begin();
}

void loop() {
  webSocket.loop();
  
  // Изпращай данни всяка секунда
  static unsigned long last = 0;
  if (millis() - last > 1000) {
    last = millis();
    
    StaticJsonDocument<128> doc;
    doc["tds"]  = readTDS();   // твоята функция
    doc["temp"] = readTemp();
    doc["ec"]   = readEC();
    
    String out;
    serializeJson(doc, out);
    webSocket.broadcastTXT(out);
  }
}
И в HTML-а замени симулираните данни с реални:
jsconst ws = new WebSocket('ws://192.168.1.XX:81');
ws.onmessage = (e) => {
  const d = JSON.parse(e.data);
  // Актуализирай DOM директно
  document.getElementById('val-tds').textContent  = d.tds;
  document.getElementById('val-temp').textContent = d.temp;
};
ws.onclose = () => console.warn('ESP офлайн');
Искаш ли да интегрирам тази WebSocket логика директно в твоя HTML файл?kide v html da smenq tova1:01 PMViewed 5 filesViewed 5 filesНамерих всичко. Ето точно къде са важните места:Find settings page WS connection section in HTMLFind settings page WS connection section in HTMLСтрахотно — сега имам пълната картина. Ето точно три места в кода, където трябва да смениш нещата:

1. Ред 1314 — IP адреса на ESP-то (в <script>):
js// Намери това:
const ESP_WS_DEFAULT = 'ws://192.168.1.101:81';

// Смени на IP-то на твоя ESP:
const ESP_WS_DEFAULT = 'ws://192.168.X.X:81';
2. Ред 1184 — Полето в Settings страницата (в <input>):
html<!-- Намери това: -->
<input class="form-input" id="ws-input" value="ws://192.168.1.101:81" style="flex:1">

<!-- Смени value= на същото IP: -->
<input class="form-input" id="ws-input" value="ws://192.168.X.X:81" style="flex:1">
3. Как да намериш IP-то на ESP-то:
В Arduino Serial Monitor при стартиране добави:
cppSerial.println(WiFi.localIP());
Ще изпише нещо като 192.168.1.47 — това е твоето IP.

Важно: И двете места трябва да съвпадат. След като смениш IP-то, отвори файла в браузъра → Settings → натисни 🔄 Свържи. Ако ESP-то е включено и изпраща данни по WebSocket на порт 81, ще видиш ESP СВЪРЗАН в горния ляв ъгъл.
Имаш ли вече готов Arduino код за ESP-то?a za esp trqbva li nestho drugo osven ip adrees kakiv kod trqbva da se pisi1:06 PMДа, трябва ти пълен Arduino код. Ето го:
cpp#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ─── СМЕНИ ТЕЗИ ───────────────────────────────
const char* WIFI_SSID = "ИМЕ_НА_WIFI";
const char* WIFI_PASS = "ПАРОЛА_НА_WIFI";
// ──────────────────────────────────────────────

// Пинове
#define TDS_PIN    A0   // Аналогов вход за TDS/EC сензор
#define TEMP_PIN   D2   // DS18B20 temperature
#define RELAY1_PIN D5   // Реле 1
#define RELAY2_PIN D6   // Реле 2  
#define RELAY3_PIN D7   // Реле 3

OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);
WebSocketsServer webSocket(81);

float readTDS() {
  int raw = analogRead(TDS_PIN);
  float voltage = raw * 3.3 / 1024.0;
  // DFRobot формула — калибрирай ако трябва
  float tds = (133.42 * voltage * voltage * voltage
             - 255.86 * voltage * voltage
             + 857.39 * voltage) * 0.5;
  return tds / 1000.0; // ppt
}

float readTemp() {
  tempSensor.requestTemperatures();
  float t = tempSensor.getTempCByIndex(0);
  return (t == -127.0) ? 0.0 : t;
}

float readEC() {
  return readTDS() * 2.0; // приблизително
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_TEXT) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      
      // Команди от HTML за релета
      const char* relay = doc["relay"];
      const char* state = doc["state"];
      if (relay && state) {
        int pin = 0;
        if      (strcmp(relay, "r1") == 0) pin = RELAY1_PIN;
        else if (strcmp(relay, "r2") == 0) pin = RELAY2_PIN;
        else if (strcmp(relay, "r3") == 0) pin = RELAY3_PIN;
        if (pin) digitalWrite(pin, strcmp(state, "on") == 0 ? LOW : HIGH);
      }

      // Emergency stop
      if (doc["emergency"]) {
        digitalWrite(RELAY1_PIN, HIGH);
        digitalWrite(RELAY2_PIN, HIGH);
        digitalWrite(RELAY3_PIN, HIGH);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY1_PIN, OUTPUT); digitalWrite(RELAY1_PIN, HIGH); // HIGH = изкл
  pinMode(RELAY2_PIN, OUTPUT); digitalWrite(RELAY2_PIN, HIGH);
  pinMode(RELAY3_PIN, OUTPUT); digitalWrite(RELAY3_PIN, HIGH);

  tempSensor.begin();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Свързване с WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nIP: " + WiFi.localIP().toString()); // <-- виж това в Serial Monitor

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket сървър стартиран на порт 81");
}

void loop() {
  webSocket.loop();

  static unsigned long lastSend = 0;
  if (millis() - lastSend >= 3000) { // Праща на всеки 3 секунди
    lastSend = millis();

    float tds  = readTDS();
    float temp = readTemp();
    float ec   = readEC();

    StaticJsonDocument<128> doc;
    doc["tds"]  = round(tds  * 100) / 100.0;
    doc["temp"] = round(temp * 10)  / 10.0;
    doc["ec"]   = round(ec   * 100) / 100.0;
    doc["wl"]   = 85; // ако нямаш water level сензор — фиксирана стойност
    doc["r1"]   = digitalRead(RELAY1_PIN) == LOW;
    doc["r2"]   = digitalRead(RELAY2_PIN) == LOW;
    doc["r3"]   = digitalRead(RELAY3_PIN) == LOW;

    String json;
    serializeJson(doc, json);
    webSocket.broadcastTXT(json);

    Serial.println(json); // за дебъг в Serial Monitor
  }
}

Библиотеки — инсталирай ги в Arduino IDE:
БиблиотекаКакESP8266WiFiидва с ESP8266 board packageWebSocketsServerArduino IDE → Library Manager → "WebSockets" от Markus SattlerArduinoJsonLibrary Manager → "ArduinoJson" от Benoit BlanchonDallasTemperatureLibrary Manager → "DallasTemperature"OneWireLibrary Manager → "OneWire"

Board settings в Arduino IDE:

Board: NodeMCU 1.0 (ESP-12E Module)
Upload Speed: 115200
Flash Size: 4MB


Важно за релетата: Повечето реле модули работят с LOW = включено и HIGH = изключено. Ако твоите са обратните — смени LOW/HIGH в кода.
Имаш ли DS18B20 температурен сензор или друг модел?az nqmam releta nqma da gi polzavAM1:07 PMОкей, много по-прост код тогава — само сензори и WebSocket:
cpp#include <ESP8266WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ─── СМЕНИ ТЕЗИ ───────────────────────────────
const char* WIFI_SSID = "ИМЕ_НА_WIFI";
const char* WIFI_PASS = "ПАРОЛА_НА_WIFI";
// ──────────────────────────────────────────────

#define TDS_PIN   A0
#define TEMP_PIN  D2

OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);
WebSocketsServer webSocket(81);

float readTDS() {
  int raw = analogRead(TDS_PIN);
  float voltage = raw * 3.3 / 1024.0;
  float tds = (133.42 * voltage * voltage * voltage
             - 255.86 * voltage * voltage
             + 857.39 * voltage) * 0.5;
  return tds / 1000.0; // ppt
}

float readTemp() {
  tempSensor.requestTemperatures();
  float t = tempSensor.getTempCByIndex(0);
  return (t == -127.0) ? 0.0 : t;
}

void setup() {
  Serial.begin(115200);
  tempSensor.begin();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Свързване");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nIP: " + WiFi.localIP().toString());

  webSocket.begin();
  Serial.println("WebSocket готов на порт 81");
}

void loop() {
  webSocket.loop();

  static unsigned long lastSend = 0;
  if (millis() - lastSend >= 3000) {
    lastSend = millis();

    float tds  = readTDS();
    float temp = readTemp();

    StaticJsonDocument<128> doc;
    doc["tds"]  = round(tds  * 100) / 100.0;
    doc["temp"] = round(temp * 10)  / 10.0;
    doc["ec"]   = round(tds * 2.0 * 100) / 100.0; // приблизително от TDS
    doc["wl"]   = 85; // нямаш сензор — фиксирана стойност

    String json;
    serializeJson(doc, json);
    webSocket.broadcastTXT(json);

    Serial.println(json);
  }
}

Също трябва да изключиш релетата в HTML-а за да не показва бутони без функция. В proekttt.html намери реда:
jsdoc["r1"] = ...
Не, по-лесно — в Settings страницата просто скрий секцията с релетата. Намери в HTML-а:
html<div class="card-title"><span class="icon">🔌</span>
И добави style="display:none" на обграждащия <div class="card">.

Какви точно сензори имаш — само TDS + DS18B20, или има нещо друго?
