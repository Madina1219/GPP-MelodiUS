// MODULE_2
#include <WiFiManager.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"


const int hallPins[]     = {34, 35};
const int solenoidPins[] = {17, 16};
const int NUM_SENSORS    = 2;

const int I2S_BCLK  =  26;
const int I2S_LRCK  =  25;
const int I2S_DIN   =  22;

#define RESET_PIN 4  // BOOT button → hold 3s to wipe WiFi + reopen portal

unsigned long solenoidOnTime[NUM_SENSORS] = {0, 0};
bool          solenoidActive[NUM_SENSORS] = {false, false};
#define SOLENOID_DURATION 500  // ms

// --- MQTT & WIFI CONFIGURATION ---
const char* mqtt_username = SECRET_MQTTUSER;
const char* mqtt_password = SECRET_MQTTPASS;

const char* mqtt_server     = "mqtt.cetools.org";
const int   mqtt_port       = 1884;
const char* mqttTopic_pub   = "student/ucfnake/solenoid/trigger_module_2";
const char* mqttTopic_sub   = "student/ucfnake/solenoid/trigger_module_1";

WiFiClient espClient;
PubSubClient mqtt(espClient);

bool lastHallState[NUM_SENSORS];
unsigned long lastReconnectAttempt = 0;

void startWiFi() {
  WiFiManager wm;
  
  wm.setCustomHeadElement("<style>body{display:none}</style>"); // hide default styles
  // wm.setCustomMenuHTML(PORTAL_HTML);

  // wm.resetSettings();

  wm.setConfigPortalTimeout(180);   // portal closes after 3 min if no one connects
  wm.setConnectTimeout(20);         // seconds to attempt WiFi connection

  Serial.println("[WiFi] Starting...");

  bool connected = wm.autoConnect("ESP32-Module2");  // AP name shown on phone

  if (!connected) {
    Serial.println("[WiFi] Portal timed out — restarting");
    ESP.restart();
  }

  Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
}

bool reconnectMQTT() {
  if (mqtt.connected()) return true;
  if (millis() - lastReconnectAttempt < 3000) return false;
  lastReconnectAttempt = millis();

  Serial.print("[MQTT] Connecting...");
  if (mqtt.connect("module2", mqtt_username, mqtt_password)) {
    Serial.println(" connected ✓");
    mqtt.subscribe(mqttTopic_sub);
    Serial.printf("[MQTT] Subscribed to: %s\n", mqttTopic_sub);
    return true;
  }

  Serial.printf(" failed rc=%d\n", mqtt.state());
  return false;
}

void sendSensorIndex(int idx) {
  char payload[10];
  snprintf(payload, sizeof(payload), "%d", idx);
  mqtt.publish(mqttTopic_pub, payload);
  Serial.printf("Published sensor idx: %d → topic: %s\n", idx, mqttTopic_pub);
}

void triggerSolenoid(int idx) {
  if (idx < 0 || idx >= NUM_SENSORS) {
    Serial.printf("Invalid solenoid idx: %d\n", idx);
    return;
  }
  Serial.printf("Triggering solenoid idx: %d\n", idx);
  digitalWrite(solenoidPins[idx], HIGH);
  solenoidOnTime[idx] = millis();
  solenoidActive[idx] = true;
}

// Add this function — call it every loop()
void updateSolenoids() {
  for (int i = 0; i < NUM_SENSORS; i++) {
    if (solenoidActive[i] && millis() - solenoidOnTime[i] >= SOLENOID_DURATION) {
      digitalWrite(solenoidPins[i], LOW);
      solenoidActive[i] = false;
      Serial.printf("Solenoid idx %d OFF\n", i);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char msg[10] = {0};
  memcpy(msg, payload, min(length, sizeof(msg) - 1));

  int idx = atoi(msg);
  Serial.printf("Received idx: %d from topic: %s\n", idx, topic);
  triggerSolenoid(idx);   // fire solenoid based on received index
}

void checkResetButton() {
  if (digitalRead(RESET_PIN) == LOW) {
    unsigned long held = millis();
    Serial.println("[Reset] Button held...");
    while (digitalRead(RESET_PIN) == LOW) {
      if (millis() - held > 3000) {
        Serial.println("[Reset] Wiping WiFi credentials — restarting");
        WiFiManager wm;
        wm.resetSettings();
        ESP.restart();
      }
    }
  }
}


void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NUM_SENSORS; i++) {
    pinMode(hallPins[i], INPUT_PULLUP);
    pinMode(solenoidPins[i], OUTPUT);
    digitalWrite(solenoidPins[i], LOW);
    lastHallState[i] = HIGH;
  }

  startWiFi();
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);
}

void loop() {
  checkResetButton();
if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Lost connection — reconnecting");
    WiFi.reconnect();
    delay(5000);
    return;
  }  

  reconnectMQTT();
  mqtt.loop();
  updateSolenoids();  // ← add this line

  for (int i = 0; i < NUM_SENSORS; i++) {
    bool currentState = digitalRead(hallPins[i]);
    if (currentState == LOW && lastHallState[i] == HIGH) {
      sendSensorIndex(i);
    }
    lastHallState[i] = currentState;
  }

  delay(20);
}
