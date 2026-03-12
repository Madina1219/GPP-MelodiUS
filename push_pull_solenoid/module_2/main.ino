// MODULE_2

#include <WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"

const int hallPins[]     = {9, 21};
const int solenoidPins[] = {17, 16};
const int NUM_SENSORS    = 2;

unsigned long solenoidOnTime[NUM_SENSORS] = {0, 0};
bool          solenoidActive[NUM_SENSORS] = {false, false};
#define SOLENOID_DURATION 500  // ms

// --- MQTT & WIFI CONFIGURATION ---
const char* ssid            = SECRET_SSID;
const char* password        = SECRET_PASS;
const char* mqtt_username = SECRET_MQTTUSER;
const char* mqtt_password = SECRET_MQTTPASS;

const char* mqtt_server     = "mqtt.cetools.org";
const int   mqtt_port       = 1884;
const char* mqttTopic_pub   = "student/ucfnake/solenoid/trigger_module_2";
const char* mqttTopic_sub   = "student/ucfnake/solenoid/trigger_module_1";

WiFiClient espClient;
PubSubClient client(espClient);

bool lastHallState[NUM_SENSORS];

// ---------------------------------------------------------
// WiFi & MQTT
// ---------------------------------------------------------
void connectWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect("module2", mqtt_username, mqtt_password)) {      // module1 unique ID
      Serial.println("connected");
      client.subscribe(mqttTopic_sub);    // subscribe to module 2's topic
      Serial.printf("Subscribed to: %s\n", mqttTopic_sub);
    } else {
      Serial.printf("failed rc=%d, retrying...\n", client.state());
      delay(2000);
    }
  }
}

// ---------------------------------------------------------
// Hall sensor triggered → publish index to module 2
// ---------------------------------------------------------
void sendSensorIndex(int idx) {
  char payload[10];
  snprintf(payload, sizeof(payload), "%d", idx);
  client.publish(mqttTopic_pub, payload);
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

// ---------------------------------------------------------
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NUM_SENSORS; i++) {
    pinMode(hallPins[i], INPUT);
    pinMode(solenoidPins[i], OUTPUT);
    digitalWrite(solenoidPins[i], LOW);
    lastHallState[i] = HIGH;
  }

  connectWiFi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
}

void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();
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
