/*
 * breathing.h — PanicSense Breathing Pacer Interface
 * ====================================================
 * Box breathing animation for the OLED display during EPISODE_ACTIVE.
 * Pattern: 4s inhale → 4s hold → 4s exhale → 4s hold (×3 cycles = ~48s).
 * Uses a visually expanding/contracting rectangle with phase labels.
 * Entirely non-blocking — call update() from loop().
 */

#ifndef BREATHING_H
#define BREATHING_H

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include "config.h"

// ─── Breathing phase enum ──────────────────────────────
enum BreathPhase {
  BREATH_PHASE_INHALE,
  BREATH_PHASE_HOLD_IN,
  BREATH_PHASE_EXHALE,
  BREATH_PHASE_HOLD_OUT
};

// ─── Start Breathing Session ───────────────────────────
// Resets internal state and begins a new breathing pacer session.
// Call once when entering EPISODE_ACTIVE state.
void breathingStart();

// ─── Update Breathing Animation ────────────────────────
// Call from loop() every iteration during EPISODE_ACTIVE state.
// Draws the current breathing frame to the OLED.
// Input: oled — reference to initialized SSD1306 display.
// Returns: true if breathing session is still active,
//          false if all cycles are complete.
bool breathingUpdate(Adafruit_SSD1306 &oled);

// ─── Get Current Phase ─────────────────────────────────
// Returns the current breathing phase (INHALE, HOLD, EXHALE, etc.).
BreathPhase breathingGetPhase();

// ─── Get Current Cycle ─────────────────────────────────
// Returns the current cycle number (1-indexed, up to BREATH_CYCLES).
int breathingGetCycle();

// ─── Is Breathing Complete ─────────────────────────────
// Returns true if all breathing cycles have completed.
bool breathingIsComplete();

#endif // BREATHING_H
