// #include "solenoid.h"
// #include "melody.h"
// #include "lights.h"
// #include "config.h"
// #include <Arduino.h>
// #include <esp_timer.h>

// // ── One timer handle per solenoid ────────────────────────
// static esp_timer_handle_t solenoidTimer[NUM_SENSORS] = { NULL, NULL };
// static bool               solenoidActive[NUM_SENSORS] = { false, false };

// // ─────────────────────────────────────────────────────────
// // Timer callback — runs on ESP timer task, not loop()
// // so it fires accurately even while playTone() is blocking
// // ─────────────────────────────────────────────────────────
// static void IRAM_ATTR onSolenoidTimer(void* arg) {
//   int idx = (int)(intptr_t)arg;
//   digitalWrite(solenoidPins[idx], LOW);
//   solenoidActive[idx]       = false;
//   solenoidSoundRequest[idx] = false;
//   Serial.printf("[Solenoid] idx %d OFF (timer)\n", idx);

//   // Return to idle green if nothing else active
//   bool anyActive = false;
//   for (int j = 0; j < NUM_SENSORS; j++) {
//     if (solenoidActive[j]) { anyActive = true; break; }
//   }
//   if (!anyActive) setLedState(LED_WIFI_CONNECTED);
// }

// // ─────────────────────────────────────────────────────────
// // Call once in setup() — creates a one-shot timer per solenoid
// // ─────────────────────────────────────────────────────────
// void solenoidSetup() {
//   for (int i = 0; i < NUM_SENSORS; i++) {
//     esp_timer_create_args_t args = {
//       .callback        = onSolenoidTimer,
//       .arg             = (void*)(intptr_t)i,
//       .dispatch_method = ESP_TIMER_TASK,
//       .name            = (i == 0 ? "sol0" : "sol1"),
//       .skip_unhandled_events = false,
//     };
//     esp_timer_create(&args, &solenoidTimer[i]);
//   }
//   Serial.println("[Solenoid] Timers created");
// }

// // ─────────────────────────────────────────────────────────
// // Fire solenoid idx — timer cuts it off after SOLENOID_DURATION
// // regardless of what loop() is doing
// // ─────────────────────────────────────────────────────────
// void triggerSolenoid(int idx) {
//   if (idx < 0 || idx >= NUM_SENSORS) return;

//   // Stop any existing timer for this solenoid before re-firing
//   esp_timer_stop(solenoidTimer[idx]);

//   Serial.printf("[Solenoid] Firing idx %d (pin %d)\n", idx, solenoidPins[idx]);
//   digitalWrite(solenoidPins[idx], HIGH);
//   solenoidActive[idx]       = true;
//   solenoidSoundRequest[idx] = true;
//   setLedState(idx == 0 ? LED_MELODY_0 : LED_MELODY_1);

//   // Schedule cutoff after SOLENOID_DURATION ms
//   esp_timer_start_once(solenoidTimer[idx], SOLENOID_DURATION * 1000ULL); // µs
// }

// // ─────────────────────────────────────────────────────────
// // updateSolenoids() kept for API compatibility but no longer
// // needed — timers handle cutoff independently
// // ─────────────────────────────────────────────────────────
// void updateSolenoids() { }
#include "solenoid.h"
#include "melody.h"
#include "lights.h"
#include "config.h"
#include <Arduino.h>

static unsigned long solenoidOnTime[NUM_SENSORS] = { 0, 0 };
static bool          solenoidActive[NUM_SENSORS] = { false, false };

// ─────────────────────────────────────────────────────────
// Fire solenoid idx and start its melody
// ─────────────────────────────────────────────────────────
void triggerSolenoid(int idx) {
  if (idx < 0 || idx >= NUM_SENSORS) return;
  Serial.printf("[Solenoid] Firing idx %d\n", idx);
  digitalWrite(solenoidPins[idx], HIGH);
  solenoidOnTime[idx]       = millis();
  solenoidActive[idx]       = true;
  solenoidSoundRequest[idx] = true;
  setLedState(idx == 0 ? LED_MELODY_0 : LED_MELODY_1);
}

// ─────────────────────────────────────────────────────────
// Call every loop() — cuts power after SOLENOID_DURATION ms
// ─────────────────────────────────────────────────────────
void updateSolenoids() {
  for (int i = 0; i < NUM_SENSORS; i++) {
    if (solenoidActive[i] && millis() - solenoidOnTime[i] >= SOLENOID_DURATION) {
      digitalWrite(solenoidPins[i], LOW);
      solenoidActive[i]       = false;
      solenoidSoundRequest[i] = false;
      Serial.printf("[Solenoid] idx %d OFF\n", i);

      // Return to idle green if no other sensor is still active
      bool anyActive = false;
      for (int j = 0; j < NUM_SENSORS; j++) {
        if (solenoidActive[j] || solenoidSoundRequest[j]) { anyActive = true; break; }
      }
      if (!anyActive) setLedState(LED_WIFI_CONNECTED);
    }
  }
}