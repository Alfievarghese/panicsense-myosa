/*
 * pulse.h — PanicSense Pulse Detection Interface
 * ================================================
 * Experimental PPG repurposing of APDS9960 proximity sensor — not medical
 * grade.
 *
 * Uses the APDS9960 onboard IR LED and photodiode in proximity mode
 * to detect pulse waveform when a finger is placed over the sensor.
 * This is a creative reuse of the MYOSA platform beyond its documented spec.
 *
 * Algorithm uses peak detection on smoothed proximity readings to
 * estimate BPM.
 */

#ifndef PULSE_H
#define PULSE_H

#include "config.h"
#include <Arduino.h>
#include <SparkFun_APDS9960.h>

// ─── Pulse detection result codes ──────────────────────
enum PulseResult {
  PULSE_OK,           // Valid BPM measurement obtained
  PULSE_QUALITY_FAIL, // Signal too noisy, not enough peaks
  PULSE_TIMEOUT,      // Sampling timed out
  PULSE_SENSOR_ERROR  // APDS9960 read error
};

// ─── Initialization ────────────────────────────────────
// Initializes the APDS9960 sensor.
// Returns true on success, false if sensor not found.
bool pulseInit(SparkFun_APDS9960 &apds);

// ─── Mode Switching ────────────────────────────────────
// Switches the APDS9960 between gesture and proximity modes.
// gestureMode = true:  enable gesture detection, disable proximity.
// gestureMode = false: enable proximity sensing, disable gesture.
// CRITICAL: APDS9960 cannot run both modes simultaneously.
void setAPDSMode(SparkFun_APDS9960 &apds, bool gestureMode);

// ─── Callback Type ─────────────────────────────────────
// Callback type for updating UI during the blocking pulse measurement
typedef void (*PulseCallback)();

// ─── Pulse Measurement (Non-Blocking UI Update) ───────
// Samples proximity for ~10 seconds, applies smoothing, detects
// peaks, and computes BPM.
// Input:  apds — reference to initialized APDS9960 object.
// Output: bpmOut — pointer to float where BPM will be stored.
//         cb — optional callback called every 20ms sample to update UI.
// Returns: PulseResult indicating success or failure mode.
PulseResult measurePulse(SparkFun_APDS9960 &apds, float *bpmOut, PulseCallback cb = nullptr);

// ─── Check for Gesture ────────────────────────────────
// Checks if a gesture swipe has been detected.
// Call only when APDS9960 is in gesture mode.
// Returns true if any swipe gesture was detected (UP, DOWN, LEFT, RIGHT).
bool checkGestureSwipe(SparkFun_APDS9960 &apds);

#endif // PULSE_H
