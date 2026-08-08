/*
 * breathing.cpp — PanicSense Breathing Pacer Implementation
 * ==========================================================
 * Implements box breathing animation on the SSD1306 OLED
 * using concentric circles that expand and contract smoothly.
 *
 * Visual: Concentric circles that grow during inhale, stay at max
 * during hold, shrink during exhale, and stay at min during pause.
 * Text label shows current phase: BREATHE IN / HOLD / BREATHE OUT
 *
 * Pattern per cycle:
 *   4s INHALE  → circles expand outward
 *   4s HOLD    → circles stay at max, label "HOLD"
 *   4s EXHALE  → circles shrink inward
 *   4s HOLD    → circles stay at min, label "HOLD"
 *
 * 3 cycles = 48 seconds total.
 * All timing uses millis() — completely non-blocking.
 */

#include "breathing.h"

// ─── Internal State ────────────────────────────────────
static unsigned long sessionStartTime = 0;
static bool sessionActive = false;
static int currentCycle = 0;
static BreathPhase currentPhase = BREATH_PHASE_INHALE;
static bool sessionComplete = false;

static const unsigned long CYCLE_DURATION_MS =
    BREATH_INHALE_MS + BREATH_HOLD_MS + BREATH_EXHALE_MS + BREATH_PAUSE_MS;
static const unsigned long SESSION_DURATION_MS = CYCLE_DURATION_MS * BREATH_CYCLES;

// Circle animation parameters
static const int CX = 64;    // center X
static const int CY = 28;    // center Y
static const int R_MIN = 4;  // smallest circle radius
static const int R_MAX = 24; // largest circle radius
static const int NUM_RINGS = 3; // number of concentric circles

/*
 * breathingStart
 */
void breathingStart() {
  sessionStartTime = millis();
  sessionActive = true;
  sessionComplete = false;
  currentCycle = 0;
  currentPhase = BREATH_PHASE_INHALE;
  Serial.println(F("[BREATHING] Session started (3 cycles, 48s)"));
}

/*
 * computePhaseAndProgress
 */
static float computePhaseAndProgress(unsigned long elapsedMs, int *cycleOut, BreathPhase *phaseOut) {
  if (elapsedMs >= SESSION_DURATION_MS) {
    *cycleOut = BREATH_CYCLES - 1;
    *phaseOut = BREATH_PHASE_HOLD_OUT;
    return 1.0f;
  }

  *cycleOut = (int)(elapsedMs / CYCLE_DURATION_MS);
  if (*cycleOut >= BREATH_CYCLES) *cycleOut = BREATH_CYCLES - 1;

  unsigned long cycleElapsed = elapsedMs % CYCLE_DURATION_MS;

  if (cycleElapsed < BREATH_INHALE_MS) {
    *phaseOut = BREATH_PHASE_INHALE;
    return (float)cycleElapsed / (float)BREATH_INHALE_MS;
  }
  cycleElapsed -= BREATH_INHALE_MS;

  if (cycleElapsed < BREATH_HOLD_MS) {
    *phaseOut = BREATH_PHASE_HOLD_IN;
    return (float)cycleElapsed / (float)BREATH_HOLD_MS;
  }
  cycleElapsed -= BREATH_HOLD_MS;

  if (cycleElapsed < BREATH_EXHALE_MS) {
    *phaseOut = BREATH_PHASE_EXHALE;
    return (float)cycleElapsed / (float)BREATH_EXHALE_MS;
  }
  cycleElapsed -= BREATH_EXHALE_MS;

  *phaseOut = BREATH_PHASE_HOLD_OUT;
  return (float)cycleElapsed / (float)BREATH_PAUSE_MS;
}

/*
 * easeInOutSine — smooth sine easing for organic feel
 */
static float easeInOutSine(float t) {
  return 0.5f * (1.0f - cosf(t * 3.14159f));
}

/*
 * drawBreathingFrame
 */
static void drawBreathingFrame(Adafruit_SSD1306 &oled, BreathPhase phase, float progress, int cycle) {
  oled.clearDisplay();

  // ─── Compute circle radius ─────────────────────────
  float size = 0.0f;
  switch (phase) {
    case BREATH_PHASE_INHALE:
      size = easeInOutSine(progress);     // Smooth grow 0 → 1
      break;
    case BREATH_PHASE_HOLD_IN:
      size = 1.0f;                         // Max
      break;
    case BREATH_PHASE_EXHALE:
      size = 1.0f - easeInOutSine(progress); // Smooth shrink 1 → 0
      break;
    case BREATH_PHASE_HOLD_OUT:
      size = 0.0f;                         // Min
      break;
  }

  int radius = R_MIN + (int)((float)(R_MAX - R_MIN) * size);

  // ─── Draw Lotus Flower (overlapping circles) ─────────
  // We draw 6 overlapping "petals" that expand and contract.
  int numPetals = 6;
  for (int i = 0; i < numPetals; i++) {
    float angle = ((float)i * 360.0f / (float)numPetals) * 3.14159f / 180.0f;
    // Offset the center of each petal circle
    int px = CX + (int)(cos(angle) * ((float)radius / 2.5f));
    int py = CY + (int)(sin(angle) * ((float)radius / 2.5f));
    
    // The radius of the petal itself
    int petalRadius = (radius * 2) / 3;
    if (petalRadius > 1) {
      oled.drawCircle(px, py, petalRadius, SSD1306_WHITE);
    }
  }

  // Center core (solid circle that pulses)
  int coreRadius = radius / 4;
  if (coreRadius < 2) coreRadius = 2;
  oled.fillCircle(CX, CY, coreRadius, SSD1306_WHITE);

  // ─── Human Breathing Stick Figure ─────────────────────
  // Drawn on the left side (x=16, y=32). Arms move based on `size`.
  int sx = 14;
  int sy = 32;
  
  // Head
  oled.drawCircle(sx, sy - 8, 3, SSD1306_WHITE);
  // Body (spine)
  oled.drawLine(sx, sy - 5, sx, sy + 8, SSD1306_WHITE);
  // Legs
  oled.drawLine(sx, sy + 8, sx - 4, sy + 16, SSD1306_WHITE);
  oled.drawLine(sx, sy + 8, sx + 4, sy + 16, SSD1306_WHITE);
  
  // Arms
  // size is 0.0 to 1.0. 
  // size = 1.0 (inhaled fully): arms are UP (y offset negative)
  // size = 0.0 (exhaled fully): arms are DOWN (y offset positive)
  int armDrop = (int)((1.0f - size) * 8.0f); // 0 (up) to 8 (down)
  // Left arm
  oled.drawLine(sx, sy - 2, sx - 6, sy - 4 + armDrop, SSD1306_WHITE);
  // Right arm
  oled.drawLine(sx, sy - 2, sx + 6, sy - 4 + armDrop, SSD1306_WHITE);


  // ─── Phase label at top ────────────────────────────
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  switch (phase) {
    case BREATH_PHASE_INHALE:
      oled.setCursor(28, 0);
      oled.print(F("BREATHE IN"));
      break;
    case BREATH_PHASE_HOLD_IN:
    case BREATH_PHASE_HOLD_OUT:
      oled.setCursor(46, 0);
      oled.print(F("HOLD"));
      break;
    case BREATH_PHASE_EXHALE:
      oled.setCursor(24, 0);
      oled.print(F("BREATHE OUT"));
      break;
  }

  // ─── Cycle indicator at bottom ─────────────────────
  oled.setTextSize(1);
  oled.setCursor(4, 56);
  oled.print(F("Cycle "));
  oled.print(cycle + 1);
  oled.print(F("/"));
  oled.print(BREATH_CYCLES);

  // Progress dots
  for (int i = 0; i < BREATH_CYCLES; i++) {
    int dotX = 90 + (i * 12);
    int dotY = 58;
    if (i <= cycle) {
      oled.fillCircle(dotX, dotY, 3, SSD1306_WHITE);
    } else {
      oled.drawCircle(dotX, dotY, 3, SSD1306_WHITE);
    }
  }

  oled.display();
}

/*
 * breathingUpdate
 */
bool breathingUpdate(Adafruit_SSD1306 &oled) {
  if (!sessionActive || sessionComplete) {
    return false;
  }

  unsigned long elapsed = millis() - sessionStartTime;

  if (elapsed >= SESSION_DURATION_MS) {
    sessionComplete = true;
    sessionActive = false;
    Serial.println(F("[BREATHING] Session complete"));
    return false;
  }

  BreathPhase phase;
  int cycle;
  float progress = computePhaseAndProgress(elapsed, &cycle, &phase);

  currentCycle = cycle;
  currentPhase = phase;

  drawBreathingFrame(oled, phase, progress, cycle);
  return true;
}

BreathPhase breathingGetPhase() {
  return currentPhase;
}

int breathingGetCycle() {
  return currentCycle + 1;
}

bool breathingIsComplete() {
  return sessionComplete;
}
