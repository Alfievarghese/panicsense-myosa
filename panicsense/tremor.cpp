/*
 * tremor.cpp — PanicSense Tremor Detection Implementation
 * ========================================================
 * Implements rolling-variance tremor detection on the MPU6050
 * accelerometer. Samples every 100ms, maintains a circular buffer
 * of 10 magnitude readings, and fires tremor confirmation only
 * after TREMOR_CONFIRM_COUNT consecutive windows exceed the
 * variance threshold.
 *
 * Algorithm:
 * 1. Read accel X, Y, Z every 100ms.
 * 2. Compute magnitude: mag = sqrt(ax² + ay² + az²)
 * 3. Maintain rolling buffer of last 10 magnitudes.
 * 4. Compute variance of the buffer.
 * 5. If variance > TREMOR_THRESHOLD: increment confirm count.
 *    Else: reset confirm count to 0.
 * 6. Confirmed when confirm count >= TREMOR_CONFIRM_COUNT.
 */

#include "tremor.h"
#include <math.h>

// ─── Internal State ────────────────────────────────────
static float magBuffer[TREMOR_BUFFER_SIZE];  // circular buffer of magnitudes
static int   bufferIndex = 0;                 // next write position
static int   bufferCount = 0;                 // number of valid entries (up to TREMOR_BUFFER_SIZE)
static float currentVariance = 0.0f;          // last computed variance
static int   tremorConfirmCount = 0;          // consecutive windows above threshold
static unsigned long tremorStartTime = 0;     // when sustained tremor started
static bool  tremorActive = false;            // is tremor currently sustained

/*
 * tremorInit
 * ----------
 * Initializes the MPU6050 accelerometer and gyroscope.
 * Sets accelerometer range to ±4g (good for tremor detection,
 * not too sensitive, not too coarse).
 * Sets gyroscope range to 500 deg/s.
 * Returns: true if MPU6050 found and configured, false otherwise.
 */
bool tremorInit(Adafruit_MPU6050 &mpu) {
  if (!mpu.begin(0x69)) {
    Serial.println(F("[TREMOR] MPU6050 init FAILED on 0x69"));
    return false;
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ); // low-pass filter to reduce noise

  // Clear the buffer
  for (int i = 0; i < TREMOR_BUFFER_SIZE; i++) {
    magBuffer[i] = 0.0f;
  }
  bufferIndex = 0;
  bufferCount = 0;
  currentVariance = 0.0f;
  tremorConfirmCount = 0;
  tremorActive = false;

  Serial.println(F("[TREMOR] MPU6050 init OK (±4g, 500dps, 21Hz BW)"));
  return true;
}

/*
 * computeVariance
 * ---------------
 * Computes variance of the magnitude buffer.
 * variance = (1/N) * Σ(xi - mean)²
 * Returns: variance in g² units.
 */
static float computeVariance() {
  if (bufferCount < 2) return 0.0f;

  int n = (bufferCount < TREMOR_BUFFER_SIZE) ? bufferCount : TREMOR_BUFFER_SIZE;

  // Compute mean
  float sum = 0.0f;
  for (int i = 0; i < n; i++) {
    sum += magBuffer[i];
  }
  float mean = sum / (float)n;

  // Compute variance
  float varSum = 0.0f;
  for (int i = 0; i < n; i++) {
    float diff = magBuffer[i] - mean;
    varSum += diff * diff;
  }
  return varSum / (float)n;
}

/*
 * tremorSample
 * ------------
 * Called every TREMOR_SAMPLE_INTERVAL_MS from the main loop.
 * Reads raw accelerometer data, computes vector magnitude,
 * pushes into circular buffer, computes variance, and updates
 * the confirmation counter.
 *
 * Input: mpu — reference to initialized MPU6050 object.
 * Side effects: updates internal buffer, variance, confirm count.
 */
void tremorSample(Adafruit_MPU6050 &mpu) {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  // Acceleration values are in m/s², convert to g (divide by 9.81)
  float ax = accel.acceleration.x / 9.81f;
  float ay = accel.acceleration.y / 9.81f;
  float az = accel.acceleration.z / 9.81f;

  // Compute magnitude
  float mag = sqrtf(ax * ax + ay * ay + az * az);

  // Push into circular buffer
  magBuffer[bufferIndex] = mag;
  bufferIndex = (bufferIndex + 1) % TREMOR_BUFFER_SIZE;
  if (bufferCount < TREMOR_BUFFER_SIZE) {
    bufferCount++;
  }

  // Compute variance of the buffer
  currentVariance = computeVariance();

  // Check against threshold
  if (currentVariance > TREMOR_THRESHOLD) {
    tremorConfirmCount++;
    if (!tremorActive && tremorConfirmCount >= TREMOR_CONFIRM_COUNT) {
      tremorActive = true;
      tremorStartTime = millis() - ((unsigned long)TREMOR_CONFIRM_COUNT * TREMOR_SAMPLE_INTERVAL_MS);
    }
  } else {
    tremorConfirmCount = 0;
    tremorActive = false;
  }
}

/*
 * isTremorConfirmed
 * -----------------
 * Returns true if tremor has been sustained for at least
 * TREMOR_CONFIRM_COUNT consecutive sampling windows.
 */
bool isTremorConfirmed() {
  return tremorConfirmCount >= TREMOR_CONFIRM_COUNT;
}

/*
 * getTremorVariance
 * -----------------
 * Returns the most recently computed variance of the
 * acceleration magnitude buffer in g² units.
 */
float getTremorVariance() {
  return currentVariance;
}

/*
 * getTremorDurationMs
 * -------------------
 * Returns how long the current sustained tremor has been active
 * in milliseconds. Returns 0 if no tremor is active.
 */
unsigned long getTremorDurationMs() {
  if (!tremorActive) return 0;
  return millis() - tremorStartTime;
}

/*
 * tremorReset
 * -----------
 * Resets the tremor detection state completely.
 * Call when transitioning back to IDLE or handling false alarm.
 */
void tremorReset() {
  tremorConfirmCount = 0;
  tremorActive = false;
  tremorStartTime = 0;
  
  // CRITICAL FIX: We MUST flush the buffer. Otherwise, the 20 samples of violent 
  // shaking from the previous episode will instantly trigger a new episode as soon
  // as the device returns to IDLE state!
  bufferCount = 0;
  bufferIndex = 0;
  currentVariance = 0.0f;
}
