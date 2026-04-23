#pragma once

// ── LED states ────────────────────────────────────────────
enum LedState {
  LED_WIFI_DISCONNECTED,  // dim red — no connection
  LED_WIFI_CONNECTED,     // green — connected, idle
  LED_MELODY_0,           // purple — sensor 0 active
  LED_MELODY_1            // blue — sensor 1 active
};

void lightsSetup();
void setLedState(LedState state);
