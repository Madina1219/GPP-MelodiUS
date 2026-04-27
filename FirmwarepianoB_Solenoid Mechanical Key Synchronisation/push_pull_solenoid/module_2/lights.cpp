#include "lights.h"
#include "config.h"
#include <FastLED.h>

// ── Strip config ──────────────────────────────────────────
#define LED_PIN        13
#define NUM_LEDS       30
#define LED_TYPE       SK6812
#define COLOR_ORDER    GRB

static CRGB leds[NUM_LEDS];

// ── Colour palette ────────────────────────────────────────
// Dim red while Wi-Fi is connecting — low brightness so it
// doesn't dominate the installation before it's "live"
static const CRGB COL_WIFI_OFF  = CRGB(40,  0,   0);

// Solid green once Wi-Fi + MQTT are established
static const CRGB COL_WIFI_ON   = CRGB(0,   160, 0);

// Purple for melody 0 (sensor 0 / solenoid 0)
static const CRGB COL_MELODY_0  = CRGB(120, 0,   180);

// Blue for melody 1 (sensor 1 / solenoid 1)
static const CRGB COL_MELODY_1  = CRGB(0,   80,  220);

// ─────────────────────────────────────────────────────────
// Call once in setup() before startWiFi()
// ─────────────────────────────────────────────────────────
void lightsSetup() {
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS)
         .setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(180);

  // Start dim red — Wi-Fi not yet connected
  fill_solid(leds, NUM_LEDS, COL_WIFI_OFF);
  FastLED.show();
  Serial.println("[LED] Initialized — waiting for Wi-Fi");
}

// ─────────────────────────────────────────────────────────
// Set the entire strip to a new state colour.
// Call whenever Wi-Fi status or active melody changes.
// ─────────────────────────────────────────────────────────
void setLedState(LedState state) {
  CRGB colour;

  switch (state) {
    case LED_WIFI_DISCONNECTED: colour = COL_WIFI_OFF;  break;
    case LED_WIFI_CONNECTED:    colour = COL_WIFI_ON;   break;
    case LED_MELODY_0:          colour = COL_MELODY_0;  break;
    case LED_MELODY_1:          colour = COL_MELODY_1;  break;
    default:                    colour = COL_WIFI_OFF;  break;
  }

  fill_solid(leds, NUM_LEDS, colour);
  FastLED.show();
}
