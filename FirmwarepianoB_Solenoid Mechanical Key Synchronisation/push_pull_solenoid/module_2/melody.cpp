#include "melody.h"
#include "audio.h"
#include "config.h"
#include <driver/i2s_std.h>
#include <Arduino.h>
#include <math.h>

// ── Melody note frequencies (Hz) ─────────────────────────
static const float melody1[] = { 262.0f, 294.0f, 330.0f, 349.0f }; // C4 D4 E4 F4
static const float melody2[] = { 392.0f, 440.0f, 494.0f, 523.0f }; // G4 A4 B4 C5

static const float* TONE_FREQ[NUM_SENSORS] = { melody1, melody2 };
static const int    MELODY_LENGTH          = 4;

// ── Sound request flags (extern'd in melody.h) ────────────
bool hallSoundRequest[NUM_SENSORS]     = { false, false };
bool solenoidSoundRequest[NUM_SENSORS] = { false, false };

// ── Chunk size — write samples in blocks for reliability ──
#define CHUNK_SIZE 256

// ─────────────────────────────────────────────────────────
// Fills a buffer with one note's worth of samples and writes
// it to I²S in CHUNK_SIZE blocks — same approach as the
// working reference code, adapted for the new driver API.
// ─────────────────────────────────────────────────────────
static void playTone(float freq, int duration_ms) {
  int    totalSamples = (int)((float)SAMPLE_RATE * duration_ms / 1000.0f);
  size_t bytes_written;
  int16_t buf[CHUNK_SIZE];

  int i = 0;
  while (i < totalSamples) {
    int count = min(CHUNK_SIZE, totalSamples - i);
    for (int j = 0; j < count; j++) {
      float sine = sinf(2.0f * PI * freq * (i + j) / (float)SAMPLE_RATE);
      buf[j] = (int16_t)(sine * AMPLITUDE);
    }
    i2s_channel_write(i2s_tx_chan, buf, count * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    i += count;
  }
}

// ─────────────────────────────────────────────────────────
// Play one note per call, looping the melody while any
// request is active. auto_clear in audio.cpp handles silence
// — no enable/disable needed here.
// ─────────────────────────────────────────────────────────
void updateSound() {
  int requestIdx = -1;
  for (int i = 0; i < NUM_SENSORS; i++) {
    if (hallSoundRequest[i] || solenoidSoundRequest[i]) {
      requestIdx = i;
      break;
    }
  }

  // Nothing active — return and let DMA auto_clear output silence
  if (requestIdx == -1) return;

  static int noteIdx = 0;

  float freq = TONE_FREQ[requestIdx][noteIdx];
  Serial.printf("[Sound] melody %d, note %d — %.1f Hz\n", requestIdx, noteIdx, freq);

  playTone(freq, NOTE_DURATION_MS);

  noteIdx = (noteIdx + 1) % MELODY_LENGTH;

  // Reset to note 0 if sensor released during this note
  bool stillActive = false;
  for (int i = 0; i < NUM_SENSORS; i++) {
    if (hallSoundRequest[i] || solenoidSoundRequest[i]) { stillActive = true; break; }
  }
  if (!stillActive) noteIdx = 0;
}