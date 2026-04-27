#pragma once
#include "config.h"

// ── Per-sensor sound request flags ───────────────────────
// Set by hall sensor or solenoid logic; cleared on release.
extern bool hallSoundRequest[NUM_SENSORS];
extern bool solenoidSoundRequest[NUM_SENSORS];

// ── Sound engine ──────────────────────────────────────────
// Call every loop(). Non-blocking — writes one sine sample
// to I²S DMA per call. Advances notes on a timer.
void updateSound();
