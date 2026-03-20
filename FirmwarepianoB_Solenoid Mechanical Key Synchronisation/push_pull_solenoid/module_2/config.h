// MODULE_2

#pragma once

// ── Sensors & actuators ───────────────────────────────────
const int NUM_SENSORS        = 2;
const int hallPins[]         = {9, 21};
const int solenoidPins[]     = {17, 18};
#define   RESET_PIN            4

// ── Solenoid timing ───────────────────────────────────────
#define SOLENOID_DURATION      500   // ms — coil on-time per trigger

// ── I²S pins (MAX98357A) ──────────────────────────────────
#define I2S_BCLK               26
#define I2S_LRCK               25
#define I2S_DIN                22
#define I2S_PORT               I2S_NUM_0   // passed to i2s_new_channel only

// ── Audio quality ─────────────────────────────────────────
#define SAMPLE_RATE            44100
#define AMPLITUDE              6000  // 0–32767 — sets volume

// ── Melody timing ─────────────────────────────────────────
#define NOTE_DURATION_MS       300   // ms per note before advancing

// ── MQTT broker ───────────────────────────────────────────
#define MQTT_SERVER            "mqtt.cetools.org"
#define MQTT_PORT              1884

// ── Module identity — swap comments for Module 2 ─────────
#define WIFI_AP_NAME           "ESP32-Module2"   // "ESP32-Module2"
#define MQTT_CLIENT_ID         "module2"          // "module2"
#define MQTT_TOPIC_PUB         "student/ucfnake/solenoid/trigger_module_2"
#define MQTT_TOPIC_SUB         "student/ucfnake/solenoid/trigger_module_1"
