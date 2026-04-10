#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <DHT.h>

// ===== WIFI =====
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""

// ===== MQTT =====
const char* mqtt_server = "***.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_username = "***";
const char* mqtt_password = "***";

WiFiClientSecure espClient;
PubSubClient client(espClient);

#define TOPIC_DHT   "iot/dht"
#define TOPIC_PUMP  "iot/control/pump"
#define TOPIC_FAN   "iot/control/fan"

// ===== DHT22 =====
#define DHTPIN 14
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// ===== RELAY =====
#define RELAY_PUMP 2
#define RELAY_FAN 4

// ===== OLED =====
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ===== STATE =====
bool pumpState = false;
bool fanState = false;
bool manualPump = false;
bool manualFan = false;

// ===== TIMER =====
unsigned long lastSend = 0;
const long interval = 10000; // 10s

// ===== AUTO RESET MANUAL =====
unsigned long lastManualFan = 0;
unsigned long lastManualPump = 0;
const long manualTimeout = 60000; // 60s

// ===== MQTT RECONNECT =====
unsigned long lastReconnectAttempt = -5000;
const long reconnectDelay = 5000;

// ===== WIFI =====
void setup_wifi() {
  Serial.print("Connecting WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());
}

// ===== CALLBACK =====
void callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++) msg += (char)payload[i];

  Serial.print("Nhan: ");
  Serial.print(topic);
  Serial.print(" = ");
  Serial.println(msg);

  // ===== PUMP =====
  if (String(topic) == TOPIC_PUMP) {
    if (msg == "ON") {
      pumpState = true;
      manualPump = true;
      lastManualPump = millis();
    }
    else if (msg == "OFF") {
      pumpState = false;
      manualPump = true;
      lastManualPump = millis();
    }
    else if (msg == "AUTO") {
      manualPump = false;
    }
  }

  // ===== FAN =====
  if (String(topic) == TOPIC_FAN) {
    if (msg == "ON") {
      fanState = true;
      manualFan = true;
      lastManualFan = millis();
    }
    else if (msg == "OFF") {
      fanState = false;
      manualFan = true;
      lastManualFan = millis();
    }
    else if (msg == "AUTO") {
      manualFan = false;
    }
  }
}

// ===== RECONNECT =====
void reconnect() {
  unsigned long now = millis();
  if (now - lastReconnectAttempt < reconnectDelay) return;
  lastReconnectAttempt = now;

  Serial.print("MQTT connecting...");
  String clientId = "ESP32_" + String(random(0xffff), HEX);

  if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
    Serial.println("Connected");
    client.subscribe(TOPIC_PUMP);
    client.subscribe(TOPIC_FAN);
  } else {
    Serial.print("Failed, rc=");
    Serial.println(client.state());
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  randomSeed(micros());

  setup_wifi();

  espClient.setInsecure();
  espClient.setTimeout(10);

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setKeepAlive(60);

  dht.begin();

  pinMode(RELAY_PUMP, OUTPUT);
  pinMode(RELAY_FAN, OUTPUT);
  digitalWrite(RELAY_PUMP, LOW);
  digitalWrite(RELAY_FAN, LOW);

  Wire.begin(22, 23);
  u8g2.begin();

  Serial.println("System ready!");
}

// ===== LOOP =====
void loop() {
  // MQTT
  if (!client.connected()) reconnect();
  client.loop();

  unsigned long now = millis();

  // ===== AUTO RESET MANUAL =====
  if (manualFan && now - lastManualFan > manualTimeout) {
    manualFan = false;
    Serial.println("Fan -> AUTO (timeout)");
  }

  if (manualPump && now - lastManualPump > manualTimeout) {
    manualPump = false;
    Serial.println("Pump -> AUTO (timeout)");
  }

  // ===== READ SENSOR =====
  float temp = dht.readTemperature();
  float humi = dht.readHumidity();

  if (isnan(temp) || isnan(humi)) {
    Serial.println("Loi DHT!");
    return;
  }

  // ===== AUTO LOGIC =====
  if (!manualFan) {
    fanState = (temp > 30);
  }

  if (!manualPump) {
    if (humi < 50) pumpState = true;
    if (humi >= 80) pumpState = false;
  }

  // ===== OUTPUT =====
  digitalWrite(RELAY_FAN, fanState ? HIGH : LOW);
  digitalWrite(RELAY_PUMP, pumpState ? HIGH : LOW);

  // ===== MQTT SEND =====
  if (now - lastSend >= interval) {
    lastSend = now;

    if (client.connected()) {
      char buffer[128];
      snprintf(buffer, sizeof(buffer),
               "{\"temp\":%.1f,\"hum\":%.1f}", temp, humi);

      if (client.publish(TOPIC_DHT, buffer)) {
        Serial.print("Published: ");
        Serial.println(buffer);
      } else {
        Serial.println("Publish FAILED");
      }
    }
  }

  // ===== DEBUG =====
  Serial.print("Temp: ");
  Serial.print(temp);
  Serial.print(" | Fan: ");
  Serial.print(fanState);
  Serial.print(" | manualFan: ");
  Serial.println(manualFan);

  // ===== OLED =====
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);

  u8g2.setCursor(0, 12);
  u8g2.print("Temp: "); u8g2.print(temp, 1); u8g2.print(" C");

  u8g2.setCursor(0, 26);
  u8g2.print("Humi: "); u8g2.print(humi, 1); u8g2.print(" %");

  u8g2.setCursor(0, 42);
  u8g2.print("Pump: "); u8g2.print(pumpState ? "ON" : "OFF");
  if (manualPump) u8g2.print("[M]");

  u8g2.setCursor(0, 58);
  u8g2.print("Fan : "); u8g2.print(fanState ? "ON" : "OFF");
  if (manualFan) u8g2.print("[M]");

  u8g2.sendBuffer();
}