/*
 * tremor.h — PanicSense Tremor Detection Interface
 * ==================================================
 * MPU6050-based tremor detection using rolling variance of
 * acceleration magnitude. Filters single spikes by requiring
 * sustained tremor over consecutive windows.
 */

#ifndef TREMOR_H
#define TREMOR_H

#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "config.h"

// ─── Initialization ────────────────────────────────────
// Initializes the MPU6050 sensor on I2C.
// Sets accelerometer range to ±4g and gyro to 500 deg/s.
// Returns true on success, false if sensor not found.
bool tremorInit(Adafruit_MPU6050 &mpu);

// ─── Tremor Sampling ──────────────────────────────────
// Called every TREMOR_SAMPLE_INTERVAL_MS from the main loop.
// Reads accelerometer X, Y, Z, computes magnitude, pushes into
// rolling buffer, and computes variance.
// Input: mpu — reference to initialized MPU6050 object.
// Modifies internal state (buffer, variance, confirm count).
void tremorSample(Adafruit_MPU6050 &mpu);

// ─── Tremor Status Query ──────────────────────────────
// Returns true if tremor_confirm_count >= TREMOR_CONFIRM_COUNT
// (sustained tremor for 300ms+).
bool isTremorConfirmed();

// ─── Get Current Variance ─────────────────────────────
// Returns the current rolling variance of the magnitude buffer.
// Useful for debugging/calibration over Serial.
float getTremorVariance();

// ─── Get Tremor Duration ──────────────────────────────
// Returns how long the current tremor has been sustained in ms.
// Resets when tremor stops.
unsigned long getTremorDurationMs();

// ─── Reset Tremor State ───────────────────────────────
// Resets the confirm counter and buffer. Call when transitioning
// out of TREMOR_DETECTED or back to IDLE (false alarm).
void tremorReset();

#endif // TREMOR_H
