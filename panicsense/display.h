/*
 * display.h — PanicSense OLED Display Interface
 * ===============================================
 * All OLED drawing functions for SSD1306 128x64 display.
 * Handles boot splash, state-specific screens, progress bars,
 * and animations using non-blocking millis() timing.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "config.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>

// ─── Initialization ────────────────────────────────────
// Initializes the SSD1306 OLED display over I2C.
// Returns true if display is found and initialized, false otherwise.
bool displayInit(Adafruit_SSD1306 &oled);

// ─── Boot Splash ───────────────────────────────────────
// Shows "PanicSense\nby MANDI MASALA\nIEEE MYOSA 2026" for
// BOOT_SPLASH_DURATION_MS. This is a blocking call (only used once at boot).
void displayBootSplash(Adafruit_SSD1306 &oled);

// ─── IDLE Screen ───────────────────────────────────────
// Shows "PanicSense\n● Ready" with a dot that blinks every 2 seconds.
// Call repeatedly from loop(); uses millis() internally for animation.
void displayIdle(Adafruit_SSD1306 &oled);

// ─── WiFi Status ───────────────────────────────────────
// Shows WiFi connection status during boot.
// message: e.g. "Connecting WiFi..." or "WiFi offline\nLocal mode only"
void displayWiFiStatus(Adafruit_SSD1306 &oled, const char *message);

// ─── Tremor Detected Screen ───────────────────────────
// Shows warning icon and instruction to place finger on sensor.
void displayTremorDetected(Adafruit_SSD1306 &oled);

// ─── Confirming Screen ─────────────────────────────────
// Shows "Confirming..." with a countdown progress bar.
// remainingMs: milliseconds remaining in the confirmation window.
// totalMs: total confirmation window duration for bar calculation.
void displayConfirming(Adafruit_SSD1306 &oled, unsigned long remainingMs,
                       unsigned long totalMs);

// ─── Pulse Quality Fail ────────────────────────────────
// Shows "Adjust finger\nand hold still" when pulse signal is noisy.
void displayPulseRetry(Adafruit_SSD1306 &oled);

// ─── False Alarm ───────────────────────────────────────
// Shows "No episode detected" briefly before returning to IDLE.
void displayFalseAlarm(Adafruit_SSD1306 &oled);

// ─── Alert Sent ────────────────────────────────────────
// Shows "Alert Sent ✓\nStay calm" after episode handling completes.
void displayAlertSent(Adafruit_SSD1306 &oled);

// ─── Cooldown Screen ──────────────────────────────────
// Shows "✓ Alert Sent\nResting Xm..." with remaining time.
// remainingMs: milliseconds remaining in cooldown.
void displayCooldown(Adafruit_SSD1306 &oled, unsigned long remainingMs);

// ─── Ready Screen (post-cooldown) ─────────────────────
// Shows "Ready" briefly before clearing for IDLE.
void displayReady(Adafruit_SSD1306 &oled);

// ─── Transition Screen ──────────────────────────────────
// Creates a cool fade-out transition using a checkerboard pixel wipe.
void displayTransition(Adafruit_SSD1306 &oled);

// ─── Clear Screen ──────────────────────────────────────
// Clears the OLED display buffer and sends blank frame.
void displayClear(Adafruit_SSD1306 &oled);

#endif // DISPLAY_H
