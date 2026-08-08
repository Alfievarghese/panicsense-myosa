/*
 * panicsense.ino — PanicSense Main Firmware
 * ===========================================
 * Wrist-worn panic attack detection and de-escalation device.
 *
 * Team: MANDI MASALA
 * Event: IEEE MYOSA International Event 6.0
 * Platform: MYOSA Mini IoT Kit (ESP32)
 *
 * Hardware:
 *   - MPU6050  (0x68) Accelerometer + Gyroscope → tremor detection
 *   - APDS9960 (0x39) Gesture/Proximity → SOS gesture + pulse PPG
 *   - BMP180   (0x77) Barometric Pressure → environment logging
 *   - SSD1306  (0x3C) OLED 128x64 → UI and breathing pacer
 *   - Active Buzzer on GPIO → alert beeps via tone()
 *
 * State Machine: IDLE → TREMOR_DETECTED → CONFIRMING → EPISODE_ACTIVE →
 * COOLDOWN → IDLE
 *
 * CRITICAL: No delay() in the main loop. All timing via millis().
 * Experimental PPG repurposing of APDS9960 proximity sensor — not medical
 * grade.
 */

#include <Adafruit_BMP085_U.h>
#include <Adafruit_GFX.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <SparkFun_APDS9960.h>
#include <Wire.h>

#include "alerts.h"
#include "breathing.h"
#include "config.h"
#include "display.h"
#include "pulse.h"
#include "tremor.h"

// ═══════════════════════════════════════════════════════
//  GLOBAL OBJECTS
// ═══════════════════════════════════════════════════════

// OLED display (128x64, no reset pin)
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// MPU6050 accelerometer + gyroscope
Adafruit_MPU6050 mpu;

// APDS9960 gesture/proximity sensor
SparkFun_APDS9960 apds;

// BMP180 barometric pressure sensor (using BMP085 Unified driver)
Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(18001);

// ═══════════════════════════════════════════════════════
//  STATE MACHINE
// ═══════════════════════════════════════════════════════

DeviceState currentState = IDLE;
DeviceState previousState = IDLE; // Track state changes for entry logic

// ─── Timing variables (all millis-based) ───────────────
unsigned long lastTremorSample = 0;      // Last MPU6050 sample time
unsigned long tremorDetectedTime = 0;    // When tremor was first detected
unsigned long confirmWindowStart = 0;    // When confirmation window opened
unsigned long episodeStartTime = 0;      // When episode became active
unsigned long cooldownStartTime = 0;     // When cooldown started
unsigned long falseAlarmDisplayEnd = 0;  // When false alarm message expires
unsigned long alertSentDisplayEnd = 0;   // When "Alert Sent" message expires
unsigned long readyDisplayEnd = 0;       // When "Ready" message expires
unsigned long lastIdleDisplayUpdate = 0; // Throttle IDLE display updates

// ─── Buzzer timing (non-blocking beep sequence) ────────
int buzzerBeepsRemaining = 0;
bool buzzerToneActive = false;
unsigned long buzzerLastEvent = 0;

// ─── Episode data ──────────────────────────────────────
float episodeBpm = 0.0f;
float episodePressure = 0.0f;
float episodeTemperature = 0.0f;
unsigned long episodeTremorDuration = 0;
const char *episodeTrigger = "auto";

// ─── Episode sub-state machine ─────────────────────────
// Within EPISODE_ACTIVE, we progress through sub-phases:
enum EpisodeSubState {
  EPISODE_BUZZER,      // Playing alert beeps
  EPISODE_BREATHING,   // Running breathing pacer
  EPISODE_READING_ENV, // Reading BMP180 + building payload
  EPISODE_SENDING,     // Sending HTTP + storing SPIFFS
  EPISODE_ALERT_SHOWN  // Showing "Alert Sent ✓" for 5s
};
EpisodeSubState episodeSubState = EPISODE_BUZZER;

// ─── HTTP send state (non-blocking retry pattern) ──────
bool httpSendPending = false;
bool httpSendComplete = false;
int httpRetryCount = 0;
unsigned long httpLastAttempt = 0;
String episodePayload = "";

// ─── Sensor initialization flags ───────────────────────
bool mpuReady = false;
bool apdsReady = false;
bool bmpReady = false;
bool oledReady = false;
bool wifiConnected = false;

// ─── Confirming sub-state ──────────────────────────────
bool pulseCheckStarted = false;
bool pulseCheckDone = false;
int pulseRetryCount = 0;
float measuredBpm = 0.0f;

// ═══════════════════════════════════════════════════════
//  FORWARD DECLARATIONS
// ═══════════════════════════════════════════════════════

void handleIdle(unsigned long now);
void handleTremorDetected(unsigned long now);
void handleConfirming(unsigned long now);
void handleEpisodeActive(unsigned long now);
void handleCooldown(unsigned long now);
void handleBuzzer(unsigned long now);
void handleBreathing(unsigned long now);
void handleReadEnvironment(unsigned long now);
void handleSending(unsigned long now);
void handleAlertShown(unsigned long now);

// ═══════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════

/*
 * setup
 * -----
 * Initializes all hardware, connects WiFi, syncs NTP, and
 * shows boot splash. Transitions to IDLE state on completion.
 */
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F(""));
  Serial.println(F("═══════════════════════════════════════"));
  Serial.println(F("  PanicSense — MANDI MASALA"));
  Serial.println(F("  IEEE MYOSA International Event 6.0"));
  Serial.println(F("═══════════════════════════════════════"));

  // ─── Brute Force Sensor Power (VEXT) ───────────────
  // Some ESP32 kits use a GPIO pin to power specific sensor sockets.
  // We will set common VEXT pins HIGH (and then LOW) to see if it wakes them
  // up.

  // ─── I2C Bus ─────────────────────────────────────────
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(500); // Give all sensors half a second to power on

  // ─── MPU6050 ─────────────────────────────────────────
  Serial.println(F("[MAIN] Initializing MPU6050..."));
  mpuReady = tremorInit(mpu);

  // ─── APDS9960 ────────────────────────────────────────
  Serial.println(F("[MAIN] Initializing APDS9960..."));
  apdsReady = pulseInit(apds);

  // ─── BMP180 ──────────────────────────────────────────
  Serial.println(F("[MAIN] Initializing BMP180..."));
  if (bmp.begin()) {
    bmpReady = true;
    Serial.println(F("[MAIN] BMP180 init OK"));
  } else {
    bmpReady = false;
    Serial.println(F("[MAIN] BMP180 init FAILED"));
  }

  // ─── OLED ────────────────────────────────────────────
  // Initialize OLED LAST because Adafruit_SSD1306 can alter the I2C bus speed
  Serial.println(F("[MAIN] Initializing OLED..."));
  oledReady = displayInit(oled);
  if (oledReady) {
    displayBootSplash(oled);
  }

  // ─── Buzzer Pin ──────────────────────────────────────
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // ─── WiFi ────────────────────────────────────────────
  if (oledReady) {
    displayWiFiStatus(oled, "Connecting WiFi...");
  }
  wifiConnected = alertsWiFiConnect();

  if (wifiConnected) {
    if (oledReady)
      displayWiFiStatus(oled, "WiFi connected!");
    alertsNTPInit();
  } else {
    if (oledReady)
      displayWiFiStatus(oled, "WiFi offline\nLocal mode only");
    delay(1500); // Brief display at boot
  }

  // ─── SPIFFS ──────────────────────────────────────────
  alertsSPIFFSInit();

  // ─── Ready ───────────────────────────────────────────
  currentState = IDLE;
  previousState = COOLDOWN; // Force entry logic on first loop

  // Note: alertsBLEInit() is intentionally delayed until it's actually needed 
  // (if WiFi fails) because BLE consumes ~50KB of contiguous heap RAM, 
  // which will starve the ESP32 and cause HTTPS POST SSL handshakes to fail with error -1.
  // which will starve the ESP32 and cause HTTPS POST SSL handshakes to fail with error -1.

  Serial.println(F("[MAIN] Setup complete — entering IDLE"));
  Serial.println(F("═══════════════════════════════════════"));
}

// ═══════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════

/*
 * loop
 * ----
 * Main state machine loop. All timing via millis(), never delay().
 * States: IDLE → TREMOR_DETECTED → CONFIRMING → EPISODE_ACTIVE → COOLDOWN
 */
void loop() {
  unsigned long now = millis();

  // Update NTP periodically (non-blocking, internally throttled)
  alertsNTPUpdate();

  // =========================================================================
  // FIX START: Capture state at start of loop so transitions can trigger entry
  // logic
  // =========================================================================
  DeviceState startingState = currentState;

  // ─── State Machine ────────────────────────────────────
  switch (currentState) {

  // ═══════════════════════════════════════════════════
  //  STATE 1: IDLE
  // ═══════════════════════════════════════════════════
  case IDLE:
    handleIdle(now);
    break;

  // ═══════════════════════════════════════════════════
  //  STATE 2: TREMOR_DETECTED
  // ═══════════════════════════════════════════════════
  case TREMOR_DETECTED:
    handleTremorDetected(now);
    break;

  // ═══════════════════════════════════════════════════
  //  STATE 3: CONFIRMING
  // ═══════════════════════════════════════════════════
  case CONFIRMING:
    handleConfirming(now);
    break;

  // ═══════════════════════════════════════════════════
  //  STATE 4: EPISODE_ACTIVE
  // ═══════════════════════════════════════════════════
  case EPISODE_ACTIVE:
    handleEpisodeActive(now);
    break;

  // ═══════════════════════════════════════════════════
  //  STATE 5: COOLDOWN
  // ═══════════════════════════════════════════════════
  case COOLDOWN:
    handleCooldown(now);
    break;
  }

  // Update previousState to what it was before any transitions happened this
  // loop
  previousState = startingState;
}

// ═══════════════════════════════════════════════════════
//  STATE HANDLERS
// ═══════════════════════════════════════════════════════

/*
 * handleIdle
 * ----------
 * IDLE state handler.
 * - Samples MPU6050 every 100ms for tremor detection.
 * - Checks APDS9960 for gesture swipe (SOS override).
 * - Updates OLED with idle screen (blinking dot).
 * Transitions:
 *   → TREMOR_DETECTED: when tremor is confirmed (3 consecutive windows)
 *   → EPISODE_ACTIVE:  when gesture swipe detected (manual SOS)
 */
void handleIdle(unsigned long now) {
  // ─── Entry logic (run once on state entry) ──────────
  if (previousState != IDLE) {
    Serial.println(F("[STATE] → IDLE"));
    if (apdsReady) {
      setAPDSMode(apds, true); // Gesture mode for SOS detection
    }
    tremorReset();
  }

  // ─── MPU6050 tremor sampling every 100ms ─────────────
  if (mpuReady &&
      (now - lastTremorSample >= (unsigned long)TREMOR_SAMPLE_INTERVAL_MS)) {
    lastTremorSample = now;
    tremorSample(mpu);

    // Check for confirmed tremor
    if (isTremorConfirmed()) {
      Serial.println(
          F("[IDLE] Tremor confirmed — transitioning to TREMOR_DETECTED"));
      Serial.print(F("[IDLE] Variance: "));
      Serial.println(getTremorVariance(), 4);
      displayTransition(oled);
      tremorDetectedTime = now;
      currentState = TREMOR_DETECTED;
      return;
    }
  }

  // ─── APDS9960 gesture check (SOS manual override) ───
  if (apdsReady && checkGestureSwipe(apds)) {
    Serial.println(F("[IDLE] SOS gesture detected — direct to EPISODE_ACTIVE"));
    episodeTrigger = "manual";
    episodeBpm = 0.0f;
    episodeTremorDuration = 0;
    displayTransition(oled);
    currentState = EPISODE_ACTIVE;
    return;
  }

  // ─── OLED update (throttled to avoid flicker) ────────
  if (oledReady && (now - lastIdleDisplayUpdate >= 200)) {
    lastIdleDisplayUpdate = now;
    displayIdle(oled);
  }
}

/*
 * handleTremorDetected
 * --------------------
 * TREMOR_DETECTED state handler.
 * - Switches APDS9960 to proximity mode for pulse detection.
 * - Shows instruction on OLED to place finger on sensor.
 * - Continues sampling MPU6050 to verify tremor is sustained.
 * Transitions:
 *   → CONFIRMING: immediately (APDS9960 mode switched, window opened)
 *   → IDLE: if tremor stops before confirmation window opens
 */
void handleTremorDetected(unsigned long now) {
  // ─── Entry logic ─────────────────────────────────────
  if (previousState != TREMOR_DETECTED) {
    Serial.println(F("[STATE] → TREMOR_DETECTED"));

    // Show instruction on OLED
    if (oledReady) {
      displayTremorDetected(oled);
    }

    // Switch APDS9960 to proximity mode for pulse detection
    if (apdsReady) {
      setAPDSMode(apds, false); // Proximity mode
    }

    // Record tremor duration so far
    episodeTremorDuration = getTremorDurationMs();
    episodeTrigger = "auto";

    // Reset pulse detection state
    pulseCheckStarted = false;
    pulseCheckDone = false;
    pulseRetryCount = 0;
    measuredBpm = 0.0f;
  }

  // ─── Keep sampling MPU6050 ───────────────────────────
  if (mpuReady &&
      (now - lastTremorSample >= (unsigned long)TREMOR_SAMPLE_INTERVAL_MS)) {
    lastTremorSample = now;
    tremorSample(mpu);

    // If tremor stops, return to IDLE (false alarm)
    if (!isTremorConfirmed()) {
      Serial.println(
          F("[TREMOR_DETECTED] Tremor stopped — false alarm, back to IDLE"));
      currentState = IDLE;
      return;
    }
  }

  // ─── Transition to CONFIRMING ────────────────────────
  // Proceed immediately to CONFIRMING now that APDS9960 is in proximity mode
  confirmWindowStart = now;
  displayTransition(oled);
  currentState = CONFIRMING;
}

bool switchedToGesture = false;

// Callback to update display during blocking pulse check
void updatePulseDisplayCallback() {
  if (oledReady) {
    unsigned long elapsed = millis() - confirmWindowStart;
    unsigned long remaining = (CONFIRM_WINDOW_MS > elapsed) ? (CONFIRM_WINDOW_MS - elapsed) : 0;
    
    // Only update every ~100ms (5 frames per second) to avoid slowing down the 20ms pulse sampling
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 100) {
      displayConfirming(oled, remaining, CONFIRM_WINDOW_MS);
      lastUpdate = millis();
    }
  }
}

/*
 * handleConfirming
 * ----------------
 * CONFIRMING state handler.
 * - Runs pulse detection on APDS9960 proximity data.
 * - Shows countdown progress bar on OLED.
 * - 20-second window to confirm episode via pulse or gesture.
 * Transitions:
 *   → EPISODE_ACTIVE: if BPM > ELEVATED_BPM or gesture swipe detected
 *   → IDLE: if 20s window expires without confirmation (false alarm)
 */
void handleConfirming(unsigned long now) {
  // ─── Entry logic ─────────────────────────────────────
  if (previousState != CONFIRMING) {
    Serial.println(F("[STATE] → CONFIRMING"));
    confirmWindowStart = now;
    pulseCheckStarted = false;
    pulseCheckDone = false;
    pulseRetryCount = 0;
    switchedToGesture = false;
  }

  // ─── Check timeout ──────────────────────────────────
  unsigned long elapsed = now - confirmWindowStart;
  if (elapsed >= CONFIRM_WINDOW_MS) {
    // Timeout — false alarm
    Serial.println(
        F("[CONFIRMING] Window expired — no confirmation, false alarm"));
    if (oledReady) {
      displayFalseAlarm(oled);
    }
    falseAlarmDisplayEnd = now + FALSE_ALARM_DISPLAY_MS;

    if (apdsReady) {
      setAPDSMode(apds, true); // Back to gesture mode
    }
    tremorReset();
    currentState = IDLE;
    return;
  }

  // ─── Update OLED with countdown (when not checking pulse) ──
  unsigned long remaining = CONFIRM_WINDOW_MS - elapsed;
  if (oledReady && !pulseCheckStarted) {
    displayConfirming(oled, remaining, CONFIRM_WINDOW_MS);
  }

  // ─── Run pulse measurement (blocking ~10s) ──────────
  // Only start if we haven't started yet
  if (!pulseCheckStarted && !pulseCheckDone) {
    pulseCheckStarted = true;
    Serial.println(F("[CONFIRMING] Starting pulse measurement..."));

    float bpm = 0.0f;
    PulseResult result = measurePulse(apds, &bpm, updatePulseDisplayCallback);

    if (result == PULSE_OK) {
      measuredBpm = bpm;
      Serial.print(F("[CONFIRMING] BPM measured: "));
      Serial.println(bpm, 1);

      if (bpm > (float)ELEVATED_BPM) {
        // Elevated pulse confirmed — episode is real
        Serial.println(
            F("[CONFIRMING] Elevated BPM confirmed → EPISODE_ACTIVE"));
        episodeBpm = bpm;
        episodeTremorDuration = getTremorDurationMs();
        displayTransition(oled);
        currentState = EPISODE_ACTIVE;
        return;
      } else {
        // Pulse detected but not elevated — continue waiting
        Serial.println(
            F("[CONFIRMING] BPM normal — waiting for timeout or gesture"));
        pulseCheckDone = true;
      }
    } else if (result == PULSE_QUALITY_FAIL &&
               pulseRetryCount < PULSE_MAX_RETRIES) {
      // Signal too noisy — retry once
      pulseRetryCount++;
      pulseCheckStarted = false; // Allow re-entry
      Serial.println(F("[CONFIRMING] Pulse quality fail — retrying"));
      if (oledReady) {
        displayPulseRetry(oled);
      }
      // Brief pause before retry (using millis-aware approach)
      // The next loop iteration will re-enter and start pulse measurement again
      return;
    } else {
      // Quality fail with no retries left, or sensor error
      Serial.println(F("[CONFIRMING] Pulse detection failed — waiting for "
                       "gesture or timeout"));
      pulseCheckDone = true;
    }
  }

  // ─── After pulse check, listen for gesture swipe ────
  // Switch briefly to gesture mode to check for SOS swipe
  if (pulseCheckDone && apdsReady) {
    // Switch to gesture mode permanently for the remainder of the window
    if (!switchedToGesture) {
      setAPDSMode(apds, true); // switch to gesture
      switchedToGesture = true;
      delay(100); // give chip 100ms to switch and settle
    }

    // Only check gesture swipe every 100ms to avoid locking the loop
    static unsigned long lastGestureCheck = 0;
    if (now - lastGestureCheck >= 100) {
      lastGestureCheck = now;
      if (checkGestureSwipe(apds)) {
        Serial.println(F("[CONFIRMING] Gesture swipe → EPISODE_ACTIVE"));
        episodeTrigger = "manual";
        episodeBpm = measuredBpm;
        episodeTremorDuration = getTremorDurationMs();
        switchedToGesture = false; // reset for next time
        displayTransition(oled);
        currentState = EPISODE_ACTIVE;
        return;
      }
    }

    // Update display with remaining time
    unsigned long nowAfter = millis();
    unsigned long elapsedAfter = nowAfter - confirmWindowStart;
    if (elapsedAfter < CONFIRM_WINDOW_MS && oledReady) {
      displayConfirming(oled, CONFIRM_WINDOW_MS - elapsedAfter,
                        CONFIRM_WINDOW_MS);
    }
  }
}

/*
 * handleEpisodeActive
 * -------------------
 * EPISODE_ACTIVE state handler.
 * Sub-state machine:
 *   1. EPISODE_BUZZER:    Play 3 alert beeps (non-blocking)
 *   2. EPISODE_BREATHING: Run breathing pacer animation (~48s)
 *   3. EPISODE_READING_ENV: Read BMP180, build JSON payload
 *   4. EPISODE_SENDING:   HTTP POST + SPIFFS store (non-blocking retry)
 *   5. EPISODE_ALERT_SHOWN: Show "Alert Sent ✓" for 5 seconds
 * Transitions:
 *   → COOLDOWN: after all sub-states complete
 */
void handleEpisodeActive(unsigned long now) {
  // ─── Entry logic ─────────────────────────────────────
  if (previousState != EPISODE_ACTIVE) {
    Serial.println(F("[STATE] → EPISODE_ACTIVE"));
    episodeStartTime = now;
    episodeSubState = EPISODE_BUZZER;

    // Start buzzer sequence
    buzzerBeepsRemaining = BUZZER_BEEP_COUNT;
    buzzerToneActive = false;
    buzzerLastEvent = now;

    // Reset HTTP send state
    httpSendPending = false;
    httpSendComplete = false;
    httpRetryCount = 0;
    episodePayload = "";
  }

  // ─── Sub-state machine ──────────────────────────────
  switch (episodeSubState) {

  case EPISODE_BUZZER:
    handleBuzzer(now);
    break;

  case EPISODE_BREATHING:
    handleBreathing(now);
    break;

  case EPISODE_READING_ENV:
    handleReadEnvironment(now);
    break;

  case EPISODE_SENDING:
    handleSending(now);
    break;

  case EPISODE_ALERT_SHOWN:
    handleAlertShown(now);
    break;
  }
}

/*
 * handleBuzzer
 * ------------
 * Non-blocking buzzer beep sequence using millis().
 * Plays BUZZER_BEEP_COUNT beeps of BUZZER_BEEP_DURATION_MS
 * with BUZZER_BEEP_GAP_MS gaps between them.
 * Uses digitalWrite() — active buzzer has internal oscillator.
 */
void handleBuzzer(unsigned long now) {
  if (buzzerBeepsRemaining <= 0) {
    // All beeps done — move to breathing
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println(F("[EPISODE] Buzzer sequence complete"));
    breathingStart();
    episodeSubState = EPISODE_BREATHING;
    return;
  }

  if (!buzzerToneActive) {
    // Start a beep
    if (now - buzzerLastEvent >= (unsigned long)BUZZER_BEEP_GAP_MS) {
      digitalWrite(BUZZER_PIN, HIGH);
      buzzerToneActive = true;
      buzzerLastEvent = now;
    }
  } else {
    // Beep is playing — wait for duration to elapse
    if (now - buzzerLastEvent >= (unsigned long)BUZZER_BEEP_DURATION_MS) {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerToneActive = false;
      buzzerBeepsRemaining--;
      buzzerLastEvent = now;
    }
  }
}

/*
 * handleBreathing
 * ---------------
 * Runs the breathing pacer animation on the OLED.
 * Non-blocking — calls breathingUpdate() each loop iteration.
 * When breathing completes, moves to environment reading.
 */
void handleBreathing(unsigned long now) {
  if (!breathingUpdate(oled)) {
    // Breathing complete
    Serial.println(F("[EPISODE] Breathing pacer complete"));
    episodeSubState = EPISODE_READING_ENV;
  }
}

/*
 * handleReadEnvironment
 * ---------------------
 * Reads BMP180 pressure and temperature, then builds the
 * episode JSON payload. Transitions to SENDING.
 */
void handleReadEnvironment(unsigned long now) {
  Serial.println(F("[EPISODE] Reading environment sensors..."));

  // Read BMP180
  episodePressure = 0.0f;
  episodeTemperature = 0.0f;

  if (bmpReady) {
    sensors_event_t pressureEvent;
    if (bmp.getEvent(&pressureEvent)) {
      episodePressure = pressureEvent.pressure;
      Serial.print(F("[EPISODE] Pressure: "));
      Serial.print(episodePressure, 1);
      Serial.println(F(" hPa"));
    }

    float temp;
    bmp.getTemperature(&temp);
    episodeTemperature = temp;
    Serial.print(F("[EPISODE] Temperature: "));
    Serial.print(episodeTemperature, 1);
    Serial.println(F(" C"));
  }

  // Build payload
  episodePayload =
      alertsBuildPayload(episodeBpm, episodeTremorDuration, episodePressure,
                         episodeTemperature, episodeTrigger);

  // Move to sending
  httpSendPending = true;
  httpSendComplete = false;
  httpRetryCount = 0;
  episodeSubState = EPISODE_SENDING;
}

/*
 * handleSending
 * -------------
 * Sends the episode payload via HTTP POST with retry logic.
 * Also stores to SPIFFS. If all HTTP retries fail, BLE fallback
 * is triggered inside alertsSendHTTP().
 */
void handleSending(unsigned long now) {
  if (!httpSendComplete) {
    Serial.println(F("[EPISODE] Sending alert..."));

    // Send HTTP POST (with internal retries and BLE fallback)
    bool httpSuccess = alertsSendHTTP(episodePayload);

    if (httpSuccess) {
      Serial.println(F("[EPISODE] Alert sent via HTTP"));
    } else {
      Serial.println(F("[EPISODE] HTTP failed — BLE fallback triggered"));
    }

    // Store to SPIFFS regardless of HTTP outcome
    alertsStoreEpisode(episodePayload);

    httpSendComplete = true;

    // Show "Alert Sent" screen
    if (oledReady) {
      displayAlertSent(oled);
    }
    alertSentDisplayEnd = now + ALERT_SENT_DISPLAY_MS;
    episodeSubState = EPISODE_ALERT_SHOWN;
  }
}

/*
 * handleAlertShown
 * ----------------
 * Shows "Alert Sent ✓\nStay calm" for ALERT_SENT_DISPLAY_MS,
 * then transitions to COOLDOWN.
 */
void handleAlertShown(unsigned long now) {
  if (now >= alertSentDisplayEnd) {
    Serial.println(F("[EPISODE] Alert display done → COOLDOWN"));
    displayTransition(oled);
    currentState = COOLDOWN;
  }
}

/*
 * handleCooldown
 * --------------
 * COOLDOWN state handler.
 * - Shows countdown on OLED.
 * - No sensor sampling — system fully idle.
 * - Duration: COOLDOWN_DURATION_MS (5 minutes default).
 * Transitions:
 *   → IDLE: after cooldown expires
 */
void handleCooldown(unsigned long now) {
  // ─── Entry logic ─────────────────────────────────────
  if (previousState != COOLDOWN) {
    Serial.println(F("[STATE] → COOLDOWN"));
    cooldownStartTime = now;

    // Switch APDS9960 back to gesture mode for when we return to IDLE
    if (apdsReady) {
      setAPDSMode(apds, true);
    }
    tremorReset();
  }

  // ─── Check cooldown expiry ──────────────────────────
  unsigned long elapsed = now - cooldownStartTime;
  if (elapsed >= COOLDOWN_DURATION_MS) {
    Serial.println(F("[COOLDOWN] Complete → IDLE"));
    if (oledReady) {
      displayReady(oled);
    }
    readyDisplayEnd = now + 2000; // Show "Ready" for 2s

    // Small non-blocking wait handled by IDLE entry re-displaying
    currentState = IDLE;
    return;
  }

  // ─── Update OLED with remaining time ────────────────
  unsigned long remaining = COOLDOWN_DURATION_MS - elapsed;
  if (oledReady) {
    displayCooldown(oled, remaining);
  }
}
