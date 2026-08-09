/*
 * config.h — PanicSense Configuration
 * =====================================
 * All tunable constants for the PanicSense wrist-worn panic attack
 * detection and de-escalation device.
 *
 * Team: MANDI MASALA
 * Event: IEEE MYOSA International Event 6.0
 *
 * Hardware: MYOSA Mini IoT Kit (ESP32)
 *   - MPU6050  (0x68) Accelerometer + Gyroscope
 *   - APDS9960 (0x39) Gesture/Proximity/Light
 *   - BMP180   (0x77) Barometric Pressure
 *   - SSD1306  (0x3C) OLED Display 128x64
 *   - Active Buzzer on GPIO
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ─── WiFi ──────────────────────────────────────────────
static const char* WIFI_SSID = "Alfie";
static const char* WIFI_PASS = "12345678";
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 10000; // 10s boot timeout

// ─── Dashboard ─────────────────────────────────────────
static const char* DASHBOARD_URL = "https://dashboard-nu-umber-40.vercel.app/api/alert";
const int   HTTP_MAX_RETRIES    = 3;
const unsigned long HTTP_RETRY_DELAY_MS = 2000; // 2s between retries

// ─── NTP ───────────────────────────────────────────────
static const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET_SEC  = 0;  // Epoch must be UTC. Browser handles local timezone.
const int   DST_OFFSET_SEC  = 0;

// ─── I2C Addresses ─────────────────────────────────────
const uint8_t I2C_ADDR_MPU6050  = 0x68;
const uint8_t I2C_ADDR_APDS9960 = 0x39;
const uint8_t I2C_ADDR_BMP180   = 0x77;
const uint8_t I2C_ADDR_SSD1306  = 0x3C;

// ─── I2C Pins (ESP32 default) ──────────────────────────
const int I2C_SDA = 21;
const int I2C_SCL = 22;

// ─── OLED ──────────────────────────────────────────────
const int SCREEN_WIDTH  = 128;
const int SCREEN_HEIGHT = 64;

// ─── Buzzer ────────────────────────────────────────
// Active buzzer — has internal oscillator, driven via digitalWrite()
// Wired: SIG→D26, VIN→5V, GND→GND
const int BUZZER_PIN  = 26;
const int BUZZER_BEEP_DURATION_MS = 200;
const int BUZZER_BEEP_GAP_MS     = 150;
const int BUZZER_BEEP_COUNT      = 3;

// ─── Tremor Detection (MPU6050) ────────────────────
const float TREMOR_THRESHOLD       = 0.3;   // g^2 variance needed to trigger (lowered for easier triggering)
const int   TREMOR_CONFIRM_COUNT   = 3;     // consecutive 100ms windows (300ms sustained)
const int   TREMOR_SAMPLE_INTERVAL_MS = 100; // sample every 100ms
const int   TREMOR_BUFFER_SIZE     = 20;    // rolling buffer of magnitudes (2s window)

// ─── Pulse Detection (APDS9960 Proximity Mode) ────────
// Experimental PPG repurposing of APDS9960 proximity sensor — not medical grade.
const int   PULSE_SAMPLE_INTERVAL_MS = 20;   // 20ms between samples
const int   PULSE_SAMPLE_COUNT       = 500;  // 10 seconds of data
const int   PULSE_SMOOTH_WINDOW      = 5;    // moving average window
const int   PEAK_MIN_DELTA           = 1;    // min delta for peak detection (APDS9960 PPG signal is very weak)
const int   PEAK_NEIGHBOR_COUNT      = 5;    // compare with 5 neighbors each side
const int   PULSE_MIN_PEAKS          = 3;    // min peaks in 10s for valid signal (lowered for weak PPG)
const int   PULSE_MIN_BPM            = 40;   // quality gate lower bound
const int   PULSE_MAX_BPM            = 200;  // quality gate upper bound
const int   PULSE_MAX_RETRIES        = 1;    // retry once on quality fail
const int   PULSE_MIN_PROXIMITY      = 100;  // minimum proximity value to ensure a finger is actually on the sensor

// ─── Episode Confirmation ──────────────────────────────
const int   ELEVATED_BPM           = 60;    // BPM threshold to confirm episode (lowered for testing — raise to 100 for production)
const unsigned long CONFIRM_WINDOW_MS = 20000; // 20s confirmation window

// ─── Breathing Pacer ───────────────────────────────────
const unsigned long BREATH_INHALE_MS  = 4000; // 4s inhale
const unsigned long BREATH_HOLD_MS    = 4000; // 4s hold
const unsigned long BREATH_EXHALE_MS  = 4000; // 4s exhale
const unsigned long BREATH_PAUSE_MS   = 4000; // 4s hold after exhale
const int   BREATH_CYCLES             = 3;    // 3 full cycles ≈ 48s

// ─── Cooldown ──────────────────────────────────────────
const unsigned long COOLDOWN_DURATION_MS = 180000; // 3 minutes

// ─── SPIFFS Episode Log ────────────────────────────────
static const char* EPISODE_LOG_PATH = "/episodes.json";
const int   MAX_STORED_EPISODES = 50;

// ─── Boot Splash ───────────────────────────────────────
const unsigned long BOOT_SPLASH_DURATION_MS = 3000; // 3s splash screen

// ─── OLED Animation Timing ─────────────────────────────
const unsigned long IDLE_DOT_BLINK_MS = 2000; // dot animates every 2s
const unsigned long ALERT_SENT_DISPLAY_MS = 5000; // "Alert Sent ✓" display time
const unsigned long FALSE_ALARM_DISPLAY_MS = 2000; // "No episode detected" display

// ─── Device State Enum ─────────────────────────────────
enum DeviceState {
  IDLE,
  TREMOR_DETECTED,
  CONFIRMING,
  EPISODE_ACTIVE,
  COOLDOWN
};

#endif // CONFIG_H
