// MODULE_2

// #include <Arduino.h>
// #include <WiFiManager.h>
// #include <WiFi.h>

// #include "config.h"
// #include "audio.h"
// #include "melody.h"
// #include "mqtt.h"
// #include "solenoid.h"
// #include "lights.h"

// // ── Internal state ────────────────────────────────────────
// static bool lastHallState[NUM_SENSORS];
// static bool wifiWasConnected = false;

// // ─────────────────────────────────────────────────────────
// // Wi-Fi captive-portal onboarding
// // ─────────────────────────────────────────────────────────
// static void startWiFi() {
//   WiFiManager wm;
//   // wm.resetSettings();  // uncomment once to clear saved credentials
//   wm.setConfigPortalTimeout(180);
//   wm.setConnectTimeout(20);
//   Serial.println("[WiFi] Starting...");
//   if (!wm.autoConnect(WIFI_AP_NAME)) {
//     Serial.println("[WiFi] Portal timed out — restarting");
//     ESP.restart();
//   }
//   Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
// }

// // ─────────────────────────────────────────────────────────
// // Hold RESET_PIN LOW for 3 s to wipe Wi-Fi credentials
// // ─────────────────────────────────────────────────────────
// static void checkResetButton() {
//   if (digitalRead(RESET_PIN) == LOW) {
//     unsigned long held = millis();
//     Serial.println("[Reset] Button held...");
//     while (digitalRead(RESET_PIN) == LOW) {
//       if (millis() - held > 3000) {
//         Serial.println("[Reset] Wiping WiFi credentials — restarting");
//         WiFiManager wm;
//         wm.resetSettings();
//         ESP.restart();
//       }
//     }
//   }
// }

// // ─────────────────────────────────────────────────────────
// void setup() {
//   Serial.begin(115200);

//   pinMode(RESET_PIN, INPUT_PULLUP);

//   for (int i = 0; i < NUM_SENSORS; i++) {
//     pinMode(hallPins[i],     INPUT_PULLUP);
//     pinMode(solenoidPins[i], OUTPUT);
//     digitalWrite(solenoidPins[i], LOW);
//     lastHallState[i] = HIGH;
//   }

//   lightsSetup();          // dim red — before Wi-Fi attempt
//   i2sInit();
//   solenoidSetup();        // create esp_timer handles for solenoid cutoff
//   startWiFi();
//   setLedState(LED_WIFI_CONNECTED);  // green once connected
//   wifiWasConnected = true;
//   mqttSetup();
// }

// // ─────────────────────────────────────────────────────────
// void loop() {
//   checkResetButton();

//   // Wi-Fi watchdog
//   if (WiFi.status() != WL_CONNECTED) {
//     Serial.println("[WiFi] Lost — reconnecting");
//     WiFi.reconnect();
//     delay(5000);
//     return;
//   }

//   // Wi-Fi watchdog
//   if (WiFi.status() != WL_CONNECTED) {
//     if (wifiWasConnected) {
//       setLedState(LED_WIFI_DISCONNECTED);  // back to dim red
//       wifiWasConnected = false;
//     }
//     Serial.println("[WiFi] Lost — reconnecting");
//     WiFi.reconnect();
//     delay(5000);
//     return;
//   }

//   // Restore green if Wi-Fi just came back
//   if (!wifiWasConnected) {
//     setLedState(LED_WIFI_CONNECTED);
//     wifiWasConnected = true;
//   }

//   reconnectMQTT();
//   mqttLoop();
//   updateSolenoids();
//   updateSound();

//   // Hall sensor edge detection + LED colour per melody
//   for (int i = 0; i < NUM_SENSORS; i++) {
//     bool current = digitalRead(hallPins[i]);

//     if (current == LOW && lastHallState[i] == HIGH) {
//       hallSoundRequest[i] = true;
//       sendSensorIndex(i);
//       // Light up the colour for this sensor's melody
//       setLedState(i == 0 ? LED_MELODY_0 : LED_MELODY_1);
//     }
//     if (current == HIGH && lastHallState[i] == LOW) {
//       hallSoundRequest[i] = false;
//       // Return to idle green if no other sensor is still held
//       bool anyActive = false;
//       for (int j = 0; j < NUM_SENSORS; j++) {
//         if (hallSoundRequest[j] || solenoidSoundRequest[j]) { anyActive = true; break; }
//       }
//       if (!anyActive) setLedState(LED_WIFI_CONNECTED);
//     }

//     lastHallState[i] = current;
//   }

//   delay(20);
// }

#include <Arduino.h>
#include <WiFiManager.h>
#include <WiFi.h>

#include "config.h"
#include "audio.h"
#include "melody.h"
#include "mqtt.h"
#include "solenoid.h"
#include "lights.h"

// ── Internal state ────────────────────────────────────────
static bool lastHallState[NUM_SENSORS];
static bool wifiWasConnected = false;

// ─────────────────────────────────────────────────────────
// Wi-Fi captive-portal onboarding
// ─────────────────────────────────────────────────────────
static void startWiFi() {
  WiFiManager wm;
  // wm.resetSettings();  // uncomment once to clear saved credentials
  wm.setConfigPortalTimeout(180);
  wm.setConnectTimeout(20);
  Serial.println("[WiFi] Starting...");
  if (!wm.autoConnect(WIFI_AP_NAME)) {
    Serial.println("[WiFi] Portal timed out — restarting");
    ESP.restart();
  }
  Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
}

// ─────────────────────────────────────────────────────────
// Hold RESET_PIN LOW for 3 s to wipe Wi-Fi credentials
// ─────────────────────────────────────────────────────────
static void checkResetButton() {
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

// ─────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(RESET_PIN, INPUT_PULLUP);

  for (int i = 0; i < NUM_SENSORS; i++) {
    pinMode(hallPins[i],     INPUT_PULLUP);
    pinMode(solenoidPins[i], OUTPUT);
    digitalWrite(solenoidPins[i], LOW);
    lastHallState[i] = HIGH;
  }

  lightsSetup();          // dim red — before Wi-Fi attempt
  i2sInit();
  startWiFi();
  setLedState(LED_WIFI_CONNECTED);  // green once connected
  wifiWasConnected = true;
  mqttSetup();
}

// ─────────────────────────────────────────────────────────
void loop() {
  checkResetButton();

  // Wi-Fi watchdog
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiWasConnected) {
      setLedState(LED_WIFI_DISCONNECTED);
      wifiWasConnected = false;
    }
    Serial.println("[WiFi] Lost — reconnecting");
    WiFi.reconnect();
    delay(5000);
    return;
  }

  // Restore green if Wi-Fi just came back
  if (!wifiWasConnected) {
    setLedState(LED_WIFI_CONNECTED);
    wifiWasConnected = true;
  }

  reconnectMQTT();
  mqttLoop();
  updateSolenoids();
  updateSound();

  // Hall sensor edge detection + LED colour per melody
  for (int i = 0; i < NUM_SENSORS; i++) {
    bool current = digitalRead(hallPins[i]);

    if (current == LOW && lastHallState[i] == HIGH) {
      hallSoundRequest[i] = true;
      sendSensorIndex(i);
      setLedState(i == 0 ? LED_MELODY_0 : LED_MELODY_1);
    }
    if (current == HIGH && lastHallState[i] == LOW) {
      hallSoundRequest[i] = false;
      bool anyActive = false;
      for (int j = 0; j < NUM_SENSORS; j++) {
        if (hallSoundRequest[j] || solenoidSoundRequest[j]) { anyActive = true; break; }
      }
      if (!anyActive) setLedState(LED_WIFI_CONNECTED);
    }

    lastHallState[i] = current;
  }

  delay(20);
}