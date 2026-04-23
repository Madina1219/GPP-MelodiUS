#include "mqtt.h"
#include "config.h"
#include "solenoid.h"
#include "secrets.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>

static WiFiClient   espClient;
static PubSubClient mqtt(espClient);
static unsigned long lastReconnectAttempt = 0;

// ─────────────────────────────────────────────────────────
// Incoming message → fire the matching solenoid
// ─────────────────────────────────────────────────────────
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // ── Debug: print raw bytes received ──────────────────────
  Serial.printf("[MQTT] ← topic: %s  length: %u  raw: [", topic, length);
  for (unsigned int i = 0; i < length; i++) {
    Serial.printf("%d", payload[i]);  // print ASCII code of each byte
    if (i < length - 1) Serial.print(",");
  }
  Serial.print("] chars: '");
  for (unsigned int i = 0; i < length; i++) Serial.print((char)payload[i]);
  Serial.println("'");

  // ── Parse and validate before firing ─────────────────────
  char msg[10] = { 0 };
  memcpy(msg, payload, min(length, (unsigned int)(sizeof(msg) - 1)));

  // Trim any whitespace/newline that some brokers append
  for (int i = strlen(msg) - 1; i >= 0 && (msg[i] == '\n' || msg[i] == '\r' || msg[i] == ' '); i--) {
    msg[i] = '\0';
  }

  // Reject anything that isn't purely digits
  bool valid = strlen(msg) > 0;
  for (int i = 0; i < (int)strlen(msg); i++) {
    if (!isdigit(msg[i])) { valid = false; break; }
  }

  if (!valid) {
    Serial.printf("[MQTT] Ignored — payload '%s' is not a valid index\n", msg);
    return;
  }

  int idx = atoi(msg);
  Serial.printf("[MQTT] Parsed idx: %d — firing solenoid\n", idx);
  triggerSolenoid(idx);
}

// ─────────────────────────────────────────────────────────
// Call once in setup()
// ─────────────────────────────────────────────────────────
void mqttSetup() {
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
}

// ─────────────────────────────────────────────────────────
// Non-blocking reconnect with 3 s back-off
// ─────────────────────────────────────────────────────────
bool reconnectMQTT() {
  if (mqtt.connected()) return true;
  if (millis() - lastReconnectAttempt < 3000) return false;
  lastReconnectAttempt = millis();

  Serial.print("[MQTT] Connecting...");
  if (mqtt.connect(MQTT_CLIENT_ID, SECRET_MQTTUSER, SECRET_MQTTPASS)) {
    Serial.println(" connected ✓");
    mqtt.subscribe(MQTT_TOPIC_SUB);
    Serial.printf("[MQTT] Subscribed to: %s\n", MQTT_TOPIC_SUB);
    return true;
  }

  Serial.printf(" failed rc=%d\n", mqtt.state());
  return false;
}

// ─────────────────────────────────────────────────────────
// Publish sensor index to the other module
// ─────────────────────────────────────────────────────────
void sendSensorIndex(int idx) {
  if (!mqtt.connected()) {
    Serial.println("[MQTT] Not connected — skipping publish");
    return;
  }
  char payload[10];
  snprintf(payload, sizeof(payload), "%d", idx);
  bool ok = mqtt.publish(MQTT_TOPIC_PUB, payload);
  Serial.printf("[MQTT] → Published idx %d — %s\n", idx, ok ? "OK" : "FAILED");
}

// ─────────────────────────────────────────────────────────
// Call every loop() to process incoming messages
// ─────────────────────────────────────────────────────────
void mqttLoop() {
  mqtt.loop();
}