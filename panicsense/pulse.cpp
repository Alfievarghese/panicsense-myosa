/*
 * pulse.cpp — PanicSense Pulse Detection Implementation
 * ======================================================
 * Experimental PPG repurposing of APDS9960 proximity sensor — not medical
 * grade.
 *
 * Algorithm:
 * 1. Sample proximity register every 20ms for 10 seconds (500 samples).
 * 2. Apply simple moving average over 5 samples to smooth noise.
 * 3. Detect peaks: a peak is a sample higher than both its 5 preceding
 *    and 5 following smoothed samples by at least PEAK_MIN_DELTA.
 * 4. Calculate average interval between peaks in milliseconds.
 * 5. BPM = 60000 / avg_interval.
 * 6. Quality gate: <4 peaks, or BPM <40 or >200 → PULSE_QUALITY_FAIL.
 */

#include "pulse.h"

// ─── Internal buffers ──────────────────────────────────
static uint8_t rawSamples[PULSE_SAMPLE_COUNT]; // raw proximity readings
static float smoothed[PULSE_SAMPLE_COUNT];     // smoothed readings
static unsigned long
    sampleTimestamps[PULSE_SAMPLE_COUNT]; // timestamp per sample

/*
 * pulseInit
 * ---------
 * Initializes the APDS9960 sensor for gesture and proximity.
 * Starts in gesture mode by default (IDLE state mode).
 * Returns: true if sensor initialized successfully, false otherwise.
 */
bool pulseInit(SparkFun_APDS9960 &apds) {
  if (!apds.init()) {
    Serial.println(F("[PULSE] APDS9960 init FAILED"));
    return false;
  }

  // Start in gesture mode for IDLE state
  apds.enableGestureSensor(true);

  Serial.println(F("[PULSE] APDS9960 init OK (gesture mode)"));
  return true;
}

/*
 * setAPDSMode
 * -----------
 * Switches the APDS9960 between gesture and proximity modes.
 * CRITICAL: The APDS9960 cannot run gesture detection and proximity
 *           detection simultaneously. This function handles the switch.
 *
 * Input: apds — reference to APDS9960 object.
 *        gestureMode — true for gesture, false for proximity.
 */
void setAPDSMode(SparkFun_APDS9960 &apds, bool gestureMode) {
  if (gestureMode) {
    // Switch to gesture mode for IDLE / SOS override detection
    apds.enableProximitySensor(false);
    apds.enableGestureSensor(true);
    Serial.println(F("[PULSE] APDS9960 → GESTURE mode"));
  } else {
    // Switch to proximity mode for pulse detection
    // Use MAXIMUM sensitivity for PPG: 100mA LED + 8X gain
    apds.enableGestureSensor(false);
    apds.enableProximitySensor(true);
    apds.setProximityGain(PGAIN_8X);
    apds.setLEDDrive(LED_DRIVE_100MA);
    Serial.println(F("[PULSE] APDS9960 → PROXIMITY mode (100mA, 8X gain)"));
  }
}

/*
 * applySmoothing
 * --------------
 * Applies a simple moving average filter over the raw proximity
 * samples. Window size = PULSE_SMOOTH_WINDOW (5 samples).
 * Edge samples use available neighbors only.
 *
 * Input:  raw[]      — array of raw uint8_t proximity values.
 *         out[]      — output array of smoothed float values.
 *         count      — number of samples.
 */
static void applySmoothing(const uint8_t raw[], float out[], int count) {
  int halfWin = PULSE_SMOOTH_WINDOW / 2;
  for (int i = 0; i < count; i++) {
    float sum = 0.0f;
    int n = 0;
    int start = (i - halfWin < 0) ? 0 : (i - halfWin);
    int end = (i + halfWin >= count) ? (count - 1) : (i + halfWin);
    for (int j = start; j <= end; j++) {
      sum += (float)raw[j];
      n++;
    }
    out[i] = sum / (float)n;
  }
}

/*
 * detectPeaks
 * -----------
 * Detects peaks in the smoothed signal. A peak at index i must be
 * higher than all of its PEAK_NEIGHBOR_COUNT preceding and following
 * smoothed samples by at least PEAK_MIN_DELTA.
 *
 * Input:  data[]     — smoothed float array.
 *         count      — number of samples.
 *         peakIndices[] — output array to store indices of detected peaks.
 *         maxPeaks   — maximum number of peaks to detect.
 * Returns: number of peaks detected.
 */
static int detectPeaks(const float data[], int count, int peakIndices[],
                       int maxPeaks) {
  int peakCount = 0;
  // A heartbeat takes ~600-1000ms. A 15-sample window is 300ms (at 20ms/sample).
  // This is a good window to find a local peak.
  int window = 15; 

  // Only check samples that have enough neighbors on both sides
  for (int i = window; i < count - window; i++) {
    bool isPeak = true;
    float val = data[i];

    // Check if it's the strict maximum in the window [-window, +window]
    for (int j = 1; j <= window; j++) {
      if (data[i - j] > val || data[i + j] >= val) {
        isPeak = false;
        break;
      }
    }

    if (isPeak) {
      // It's a local maximum. Check if it's prominent enough.
      // Find the minimum in this window to calculate the delta
      float minVal = val;
      for (int j = 1; j <= window; j++) {
        if (data[i - j] < minVal) minVal = data[i - j];
        if (data[i + j] < minVal) minVal = data[i + j];
      }

      if (val - minVal >= (float)PEAK_MIN_DELTA) {
        if (peakCount < maxPeaks) {
          peakIndices[peakCount] = i;
        }
        peakCount++;
        // Skip ahead to avoid detecting multiple peaks in the same heartbeat
        i += window;
      }
    }
  }

  return peakCount;
}

/*
 * measurePulse
 * ------------
 * Performs a complete pulse measurement cycle:
 * 1. Samples proximity every 20ms for 10 seconds.
 * 2. Smooths the signal.
 * 3. Detects peaks.
 * 4. Computes BPM from average inter-peak interval.
 * 5. Validates against quality gates.
 *
 * Input:  apds   — reference to APDS9960 in proximity mode.
 *         bpmOut — pointer to store computed BPM on success.
 * Returns: PULSE_OK on valid measurement,
 *          PULSE_QUALITY_FAIL if signal is too noisy or BPM out of range,
 *          PULSE_SENSOR_ERROR if proximity read fails.
 *
 * NOTE: This function blocks for approximately 10 seconds.
 */
PulseResult measurePulse(SparkFun_APDS9960 &apds, float *bpmOut, PulseCallback cb) {
  if (!bpmOut) return PULSE_SENSOR_ERROR;

  // ─── Phase 1: Collect raw samples ─────────────────────
  // We collect PULSE_SAMPLE_COUNT (e.g. 500) samples at PULSE_SAMPLE_INTERVAL_MS
  // (e.g. 20ms). Total time: ~10 seconds.
  uint8_t rawSamples[PULSE_SAMPLE_COUNT];
  unsigned long sampleTimestamps[PULSE_SAMPLE_COUNT];
  float smoothed[PULSE_SAMPLE_COUNT];

  int sampleCount = 0;
  unsigned long sampleStart = millis();

  for (int i = 0; i < PULSE_SAMPLE_COUNT; i++) {
    unsigned long loopStart = millis();

    uint8_t proxValue = 0;
    if (!apds.readProximity(proxValue)) {
      Serial.println(F("[PULSE] Proximity read error"));
      return PULSE_SENSOR_ERROR;
    }

    rawSamples[i] = proxValue;
    sampleTimestamps[i] = millis();
    sampleCount++;
    
    // Call UI update callback if provided
    if (cb) {
      cb();
    }

    // Wait for next sample interval
    unsigned long elapsed = millis() - loopStart;
    if (elapsed < (unsigned long)PULSE_SAMPLE_INTERVAL_MS) {
      delay(PULSE_SAMPLE_INTERVAL_MS -
            elapsed); // Blocking delay OK here — UI is updated via callback
    }
  }

  unsigned long totalSampleTime = millis() - sampleStart;
  Serial.print(F("[PULSE] Sampled "));
  Serial.print(sampleCount);
  Serial.print(F(" readings in "));
  Serial.print(totalSampleTime);
  Serial.println(F("ms"));

  // ─── Phase 2: Smooth the signal ─────────────────────
  applySmoothing(rawSamples, smoothed, sampleCount);

  // ─── Debug: Print signal range to help tuning ────────
  float minVal = 255.0f, maxVal = 0.0f;
  for (int i=0; i<sampleCount; i++) {
      if (smoothed[i] < minVal) minVal = smoothed[i];
      if (smoothed[i] > maxVal) maxVal = smoothed[i];
  }
  Serial.print(F("[PULSE] Signal range: min="));
  Serial.print(minVal, 1);
  Serial.print(F(", max="));
  Serial.print(maxVal, 1);
  Serial.print(F(", delta="));
  Serial.println(maxVal - minVal, 1);

  // ─── Phase 3: Detect peaks ──────────────────────────
  const int maxPeaks = 50; // more than enough for 10s at 200 BPM
  int peakIndices[50];
  int peakCount = detectPeaks(smoothed, sampleCount, peakIndices, maxPeaks);

  Serial.print(F("[PULSE] Detected "));
  Serial.print(peakCount);
  Serial.println(F(" peaks"));

  // ─── Phase 4: Quality gate on peak count ─────────────
  if (peakCount < PULSE_MIN_PEAKS) {
    Serial.println(F("[PULSE] QUALITY_FAIL: too few peaks"));
    return PULSE_QUALITY_FAIL;
  }

  // ─── Phase 5: Compute average interval ───────────────
  unsigned long totalInterval = 0;
  int intervalCount = 0;

  for (int i = 1; i < peakCount && i < maxPeaks; i++) {
    unsigned long interval =
        sampleTimestamps[peakIndices[i]] - sampleTimestamps[peakIndices[i - 1]];
    totalInterval += interval;
    intervalCount++;
  }

  if (intervalCount == 0) {
    Serial.println(F("[PULSE] QUALITY_FAIL: no valid intervals"));
    return PULSE_QUALITY_FAIL;
  }

  float avgInterval = (float)totalInterval / (float)intervalCount;
  float bpm = 60000.0f / avgInterval;

  Serial.print(F("[PULSE] Avg interval: "));
  Serial.print(avgInterval, 1);
  Serial.print(F("ms → BPM: "));
  Serial.println(bpm, 1);

  // ─── Phase 6: Quality gate on BPM range ──────────────
  if (bpm < (float)PULSE_MIN_BPM || bpm > (float)PULSE_MAX_BPM) {
    Serial.print(F("[PULSE] QUALITY_FAIL: BPM out of range ("));
    Serial.print(bpm, 1);
    Serial.println(F(")"));
    return PULSE_QUALITY_FAIL;
  }

  *bpmOut = bpm;
  return PULSE_OK;
}

/*
 * checkGestureSwipe
 * -----------------
 * Checks if a gesture swipe has been detected on the APDS9960.
 * Must be called only when sensor is in gesture mode.
 * Accepts any directional swipe (UP, DOWN, LEFT, RIGHT) as a valid SOS gesture.
 *
 * Returns: true if any swipe detected, false otherwise.
 */
bool checkGestureSwipe(SparkFun_APDS9960 &apds) {
  if (!apds.isGestureAvailable()) {
    return false;
  }

  int gesture = apds.readGesture();
  switch (gesture) {
  case DIR_UP:
    Serial.println(F("[PULSE] Gesture: SWIPE UP"));
    return true;
  case DIR_DOWN:
    Serial.println(F("[PULSE] Gesture: SWIPE DOWN"));
    return true;
  case DIR_LEFT:
    Serial.println(F("[PULSE] Gesture: SWIPE LEFT"));
    return true;
  case DIR_RIGHT:
    Serial.println(F("[PULSE] Gesture: SWIPE RIGHT"));
    return true;
  default:
    return false;
  }
}
