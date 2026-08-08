/*
 * display.cpp — PanicSense OLED Display Implementation
 * =====================================================
 * Premium animations for the SSD1306 128x64 OLED display.
 * Uses Adafruit GFX primitives with smooth, non-blocking
 * millis()-based animations.
 *
 * Screens:
 *   - Boot Splash: animated wipe reveal + heartbeat icon
 *   - IDLE: ECG flatline with pulsing dot
 *   - Tremor Detected: shaking warning + pulsing triangle
 *   - Confirming: smooth progress bar + scanning dots
 *   - Pulse Retry: animated finger icon
 *   - False Alarm: fade-style clear
 *   - Alert Sent: animated checkmark draw
 *   - Cooldown: circular progress ring
 *   - Ready: fade-in text
 */

#include "display.h"

// ─── Internal state for animations ─────────────────────
static unsigned long lastAnimFrame = 0;
static int animFrame = 0;
static unsigned long lastDotToggle = 0;
static bool dotVisible = true;

// ─── Heartbeat icon bitmap (16x12) ─────────────────────
static const uint8_t heartBitmap[] PROGMEM = {
  0b00000000, 0b00000000,
  0b01100110, 0b00000000,
  0b11111111, 0b00000000,
  0b11111111, 0b00000000,
  0b11111111, 0b00000000,
  0b01111110, 0b00000000,
  0b00111100, 0b00000000,
  0b00011000, 0b00000000,
};

// ─── Helper: Draw a small heart at (x,y) ───────────────
static void drawHeart(Adafruit_SSD1306 &oled, int x, int y, int size) {
  // Simple heart using two circles and a triangle
  int r = size / 3;
  oled.fillCircle(x - r, y, r, SSD1306_WHITE);
  oled.fillCircle(x + r, y, r, SSD1306_WHITE);
  oled.fillTriangle(
    x - size / 2 - 1, y + r / 2,
    x + size / 2 + 1, y + r / 2,
    x, y + size,
    SSD1306_WHITE
  );
}

// ─── Helper: Draw ECG-style heartbeat line ─────────────
static void drawECGLine(Adafruit_SSD1306 &oled, int y, int phase) {
  // Draws a single-pixel ECG trace across the screen
  // phase controls where the "beat spike" is (0-127)
  for (int x = 0; x < 128; x++) {
    int xRel = (x - phase + 128) % 128;
    int yOff = 0;

    if (xRel >= 20 && xRel < 24) {
      // Small P wave
      yOff = -2;
    } else if (xRel >= 28 && xRel < 30) {
      // Q dip
      yOff = 2;
    } else if (xRel >= 30 && xRel < 33) {
      // R peak (tall spike)
      yOff = -8 + (xRel - 30) * 4;
    } else if (xRel >= 33 && xRel < 36) {
      // S dip
      yOff = 4 - (xRel - 33) * 2;
    } else if (xRel >= 42 && xRel < 50) {
      // T wave
      float t = (float)(xRel - 42) / 8.0f;
      yOff = (int)(-3.0f * sinf(t * 3.14159f));
    }

    oled.drawPixel(x, y + yOff, SSD1306_WHITE);
  }
}

// ─── Helper: Draw warning triangle ─────────────────────
static void drawWarningTriangle(Adafruit_SSD1306 &oled, int cx, int cy, int size) {
  oled.drawTriangle(
    cx, cy - size,
    cx - size, cy + size / 2,
    cx + size, cy + size / 2,
    SSD1306_WHITE
  );
  // Exclamation mark inside
  oled.drawLine(cx, cy - size / 2, cx, cy + size / 4 - 2, SSD1306_WHITE);
  oled.drawPixel(cx, cy + size / 4 + 1, SSD1306_WHITE);
}

// ─── Helper: Draw circular progress arc ────────────────
static void drawProgressArc(Adafruit_SSD1306 &oled, int cx, int cy, int r, float fraction) {
  // Draw background circle (dimmed — dotted)
  for (int angle = 0; angle < 360; angle += 6) {
    float rad = angle * 3.14159f / 180.0f;
    int x = cx + (int)(r * cosf(rad));
    int y = cy + (int)(r * sinf(rad));
    oled.drawPixel(x, y, SSD1306_WHITE);
  }
  // Draw filled arc for progress
  int endAngle = (int)(fraction * 360.0f);
  for (int angle = -90; angle < -90 + endAngle; angle++) {
    float rad = angle * 3.14159f / 180.0f;
    int x = cx + (int)(r * cosf(rad));
    int y = cy + (int)(r * sinf(rad));
    oled.drawPixel(x, y, SSD1306_WHITE);
    // Draw a thicker arc (2px)
    int x2 = cx + (int)((r - 1) * cosf(rad));
    int y2 = cy + (int)((r - 1) * sinf(rad));
    oled.drawPixel(x2, y2, SSD1306_WHITE);
  }
}

/*
 * displayInit
 * -----------
 * Initializes the SSD1306 OLED display.
 */
bool displayInit(Adafruit_SSD1306 &oled) {
  if (!oled.begin(SSD1306_SWITCHCAPVCC, I2C_ADDR_SSD1306)) {
    Serial.println(F("[DISPLAY] SSD1306 init FAILED"));
    return false;
  }
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.display();
  Serial.println(F("[DISPLAY] SSD1306 init OK"));
  return true;
}

/*
 * displayBootSplash
 * -----------------
 * Animated boot splash: horizontal wipe reveal + heartbeat icon.
 * Blocking call (only at boot).
 */
void displayBootSplash(Adafruit_SSD1306 &oled) {
  // Phase 1: Wipe-in animation (line sweeps left to right)
  for (int x = 0; x <= 128; x += 4) {
    oled.clearDisplay();

    // Draw content that's "revealed" up to x
    oled.setTextSize(2);
    oled.setCursor(10, 8);
    oled.print(F("Panic"));
    oled.setCursor(10, 28);
    oled.print(F("Sense"));

    // Draw heart icon
    drawHeart(oled, 108, 14, 8);

    oled.setTextSize(1);
    oled.setCursor(10, 48);
    oled.print(F("MANDI MASALA"));

    // Mask: black rectangle covering unrevealed area
    oled.fillRect(x, 0, 128 - x, 64, SSD1306_BLACK);

    // Wipe line
    if (x < 128) {
      oled.drawLine(x, 0, x, 63, SSD1306_WHITE);
    }

    oled.display();
    delay(15);
  }

  // Phase 2: Hold with pulsing heart
  for (int i = 0; i < 20; i++) {
    oled.clearDisplay();

    oled.setTextSize(2);
    oled.setCursor(10, 8);
    oled.print(F("Panic"));
    oled.setCursor(10, 28);
    oled.print(F("Sense"));

    // Pulsing heart (alternating sizes)
    int heartSize = (i % 4 < 2) ? 8 : 10;
    drawHeart(oled, 108, 14, heartSize);

    oled.setTextSize(1);
    oled.setCursor(10, 48);
    oled.print(F("MANDI MASALA"));
    oled.setCursor(10, 56);
    oled.print(F("IEEE MYOSA 2026"));

    // Decorative line
    oled.drawLine(10, 46, 118, 46, SSD1306_WHITE);

    oled.display();
    delay(120);
  }
}

/*
 * displayIdle
 * -----------
 * Shows "PanicSense" with animated ECG flatline trace.
 * Non-blocking.
 */
void displayIdle(Adafruit_SSD1306 &oled) {
  unsigned long now = millis();

  // Advance animation phase
  if (now - lastAnimFrame >= 50) {
    animFrame = (animFrame + 2) % 128;
    lastAnimFrame = now;
  }

  // Toggle dot
  if (now - lastDotToggle >= IDLE_DOT_BLINK_MS) {
    dotVisible = !dotVisible;
    lastDotToggle = now;
  }

  oled.clearDisplay();

  // Title
  oled.setTextSize(2);
  oled.setCursor(10, 2);
  oled.print(F("Panic"));
  oled.setCursor(10, 20);
  oled.print(F("Sense"));

  // Small heart icon
  drawHeart(oled, 108, 8, 6);

  // ECG line animation
  drawECGLine(oled, 42, animFrame);

  // Status line
  oled.setTextSize(1);
  oled.setCursor(10, 56);
  if (dotVisible) {
    oled.fillCircle(14, 59, 2, SSD1306_WHITE);
    oled.setCursor(20, 56);
    oled.print(F("Monitoring"));
  } else {
    oled.drawCircle(14, 59, 2, SSD1306_WHITE);
    oled.setCursor(20, 56);
    oled.print(F("Monitoring"));
  }

  oled.display();
}

/*
 * displayWiFiStatus
 * -----------------
 * WiFi status screen with signal icon.
 */
void displayWiFiStatus(Adafruit_SSD1306 &oled, const char* message) {
  oled.clearDisplay();

  // WiFi signal icon (3 arcs)
  int cx = 110, cy = 20;
  oled.drawCircle(cx, cy, 4, SSD1306_WHITE);
  oled.drawCircle(cx, cy, 8, SSD1306_WHITE);
  oled.drawCircle(cx, cy, 12, SSD1306_WHITE);
  oled.fillCircle(cx, cy, 2, SSD1306_WHITE);

  oled.setTextSize(1);
  oled.setCursor(4, 8);
  oled.print(F("PanicSense"));

  // Separator
  oled.drawLine(4, 20, 90, 20, SSD1306_WHITE);

  oled.setCursor(4, 30);
  oled.print(message);

  // Loading dots animation
  int dots = (millis() / 400) % 4;
  oled.setCursor(4, 50);
  for (int i = 0; i < dots; i++) {
    oled.print(F("."));
  }

  oled.display();
}

/*
 * displayTremorDetected
 * ---------------------
 * Animated warning with shaking text effect and pulsing triangle.
 */
void displayTremorDetected(Adafruit_SSD1306 &oled) {
  unsigned long now = millis();
  
  // Random full screen shake offsets (-2 to +2 pixels)
  int dx = (random(5) - 2);
  int dy = (random(5) - 2);
  bool pulse = (now / 300) % 2 == 0;

  oled.clearDisplay();

  // Pulsing warning triangle
  int triSize = pulse ? 12 : 10;
  drawWarningTriangle(oled, 64 + dx, 10 + dy, triSize);

  // Shaking text
  oled.setTextSize(2);
  oled.setCursor(12 + dx, 24 + dy);
  oled.print(F("Tremor!"));

  // Instructions
  oled.setTextSize(1);
  oled.setCursor(4 + dx, 44 + dy);
  oled.print(F("Place finger on"));
  oled.setCursor(4 + dx, 54 + dy);
  oled.print(F("sensor now"));

  // Animated arrow pointing down-right
  int arrowBounce = ((now / 200) % 3);
  oled.drawLine(110 + dx, 50 + arrowBounce + dy, 120 + dx, 58 + arrowBounce + dy, SSD1306_WHITE);
  oled.drawLine(120 + dx, 58 + arrowBounce + dy, 114 + dx, 58 + arrowBounce + dy, SSD1306_WHITE);
  oled.drawLine(120 + dx, 58 + arrowBounce + dy, 120 + dx, 52 + arrowBounce + dy, SSD1306_WHITE);

  oled.display();
}

/*
 * displayConfirming
 * -----------------
 * Smooth progress bar with scanning dots animation.
 */
void displayConfirming(Adafruit_SSD1306 &oled, unsigned long remainingMs, unsigned long totalMs) {
  unsigned long now = millis();
  oled.clearDisplay();

  // Title with scanning dots
  oled.setTextSize(1);
  oled.setCursor(4, 4);
  oled.print(F("Measuring pulse"));

  int dots = (now / 300) % 4;
  for (int i = 0; i < dots; i++) {
    oled.print(F("."));
  }

  // Animated heartbeat icon
  bool beat = (now / 500) % 2 == 0;
  drawHeart(oled, 114, 4, beat ? 5 : 4);

  // Progress bar with rounded ends
  int barX = 8;
  int barY = 22;
  int barW = 112;
  int barH = 12;

  // Background outline
  oled.drawRoundRect(barX, barY, barW, barH, 4, SSD1306_WHITE);

  // Fill
  float fraction = (float)remainingMs / (float)totalMs;
  if (fraction < 0.0f) fraction = 0.0f;
  if (fraction > 1.0f) fraction = 1.0f;
  int fillW = (int)(fraction * (barW - 6));
  if (fillW > 0) {
    oled.fillRoundRect(barX + 3, barY + 3, fillW, barH - 6, 2, SSD1306_WHITE);
  }

  // Countdown
  int secondsLeft = (int)(remainingMs / 1000);
  if (secondsLeft < 0) secondsLeft = 0;

  oled.setTextSize(2);
  oled.setCursor(40, 40);
  oled.print(secondsLeft);
  oled.setTextSize(1);
  oled.print(F(" sec"));

  // Bottom instruction
  oled.setCursor(20, 56);
  oled.print(F("Hold still"));

  oled.display();
}

/*
 * displayPulseRetry
 * -----------------
 * Retry instruction with animated finger placement icon.
 */
void displayPulseRetry(Adafruit_SSD1306 &oled) {
  oled.clearDisplay();

  // Animated circle representing sensor
  unsigned long now = millis();
  int radius = 8 + ((now / 200) % 3);

  oled.drawCircle(64, 20, radius, SSD1306_WHITE);
  oled.drawCircle(64, 20, radius - 3, SSD1306_WHITE);

  oled.setTextSize(1);
  oled.setCursor(16, 36);
  oled.print(F("Adjust finger"));
  oled.setCursor(16, 48);
  oled.print(F("and hold still"));

  // Retry indicator dots
  int dots = (now / 250) % 4;
  oled.setCursor(80, 48);
  for (int i = 0; i < dots; i++) oled.print(F("."));

  oled.display();
}

/*
 * displayFalseAlarm
 * -----------------
 * Shows gentle "All clear" message with checkmark.
 */
void displayFalseAlarm(Adafruit_SSD1306 &oled) {
  oled.clearDisplay();

  // Centered circle with check
  oled.drawCircle(64, 24, 14, SSD1306_WHITE);
  // Checkmark
  oled.drawLine(57, 24, 62, 29, SSD1306_WHITE);
  oled.drawLine(62, 29, 72, 18, SSD1306_WHITE);

  oled.setTextSize(1);
  oled.setCursor(28, 44);
  oled.print(F("All clear"));
  oled.setCursor(16, 54);
  oled.print(F("Resuming monitor"));

  oled.display();
}

/*
 * displayAlertSent
 * ----------------
 * Animated checkmark that draws itself, then "Alert Sent".
 */
void displayAlertSent(Adafruit_SSD1306 &oled) {
  // Animated checkmark draw
  for (int frame = 0; frame <= 16; frame++) {
    oled.clearDisplay();

    // Circle
    oled.drawCircle(64, 22, 16, SSD1306_WHITE);
    oled.drawCircle(64, 22, 15, SSD1306_WHITE);

    // Animated checkmark (two segments)
    // Segment 1: short downward stroke
    int seg1End = min(frame, 6);
    if (seg1End > 0) {
      oled.drawLine(54, 22, 54 + seg1End, 22 + seg1End, SSD1306_WHITE);
      oled.drawLine(54, 23, 54 + seg1End, 23 + seg1End, SSD1306_WHITE);
    }
    // Segment 2: long upward stroke
    if (frame > 6) {
      int seg2End = min(frame - 6, 10);
      oled.drawLine(60, 28, 60 + seg2End, 28 - seg2End * 2/3, SSD1306_WHITE);
      oled.drawLine(60, 29, 60 + seg2End, 29 - seg2End * 2/3, SSD1306_WHITE);
    }

    if (frame > 10) {
      oled.setTextSize(2);
      oled.setCursor(10, 44);
      oled.print(F("Alert Sent"));
    }

    oled.display();
    delay(50);
  }

  // Final hold frame
  oled.clearDisplay();
  oled.drawCircle(64, 22, 16, SSD1306_WHITE);
  oled.drawCircle(64, 22, 15, SSD1306_WHITE);
  oled.drawLine(54, 22, 60, 28, SSD1306_WHITE);
  oled.drawLine(54, 23, 60, 29, SSD1306_WHITE);
  oled.drawLine(60, 28, 70, 18, SSD1306_WHITE);
  oled.drawLine(60, 29, 70, 19, SSD1306_WHITE);
  oled.setTextSize(2);
  oled.setCursor(10, 44);
  oled.print(F("Alert Sent"));
  oled.display();
}

/*
 * displayCooldown
 * ---------------
 * Circular progress ring with remaining time.
 */
void displayCooldown(Adafruit_SSD1306 &oled, unsigned long remainingMs) {
  oled.clearDisplay();

  // Circular progress ring
  float fraction = (float)remainingMs / (float)COOLDOWN_DURATION_MS;
  if (fraction < 0.0f) fraction = 0.0f;
  if (fraction > 1.0f) fraction = 1.0f;

  drawProgressArc(oled, 28, 32, 22, fraction);

  // Time in center of ring
  unsigned long remainingSec = remainingMs / 1000;
  unsigned long remainingMin = remainingSec / 60;
  unsigned long secPart = remainingSec % 60;

  oled.setTextSize(1);
  if (remainingMin > 0) {
    oled.setCursor(20, 28);
    oled.print(remainingMin);
    oled.print(F(":"));
    if (secPart < 10) oled.print(F("0"));
    oled.print(secPart);
  } else {
    oled.setCursor(22, 28);
    oled.print(secPart);
    oled.print(F("s"));
  }

  // Right side: status text
  oled.setTextSize(1);
  oled.setCursor(54, 10);
  oled.print(F("Alert Sent"));

  // Checkmark
  oled.drawLine(54, 26, 58, 30, SSD1306_WHITE);
  oled.drawLine(58, 30, 66, 22, SSD1306_WHITE);

  oled.setCursor(60, 38);
  oled.print(F("Resting..."));

  oled.setCursor(60, 52);
  oled.print(F("Stay calm"));

  // Decorative breathing dot
  bool pulse = (millis() / 800) % 2 == 0;
  if (pulse) {
    oled.fillCircle(120, 56, 2, SSD1306_WHITE);
  } else {
    oled.drawCircle(120, 56, 2, SSD1306_WHITE);
  }

  oled.display();
}

/*
 * displayReady
 * ------------
 * Post-cooldown ready screen with gentle animation.
 */
void displayReady(Adafruit_SSD1306 &oled) {
  // Fade-in effect (progressive reveal)
  for (int step = 0; step < 4; step++) {
    oled.clearDisplay();

    if (step >= 1) {
      oled.drawCircle(64, 24, 16, SSD1306_WHITE);
    }
    if (step >= 2) {
      drawHeart(oled, 64, 22, 8);
    }
    if (step >= 3) {
      oled.setTextSize(2);
      oled.setCursor(30, 46);
      oled.print(F("Ready"));
    }

    oled.display();
    delay(200);
  }
}

/*
 * displayTransition
 * -----------------
 * Creates a cool fade-out transition using a checkerboard pixel wipe.
 */
void displayTransition(Adafruit_SSD1306 &oled) {
  for (int step = 0; step < 4; step++) {
    for (int y = 0; y < 64; y += 2) {
      for (int x = 0; x < 128; x += 2) {
        if ((x + y) % 8 == step * 2) {
          oled.drawPixel(x, y, SSD1306_BLACK);
          oled.drawPixel(x+1, y, SSD1306_BLACK);
          oled.drawPixel(x, y+1, SSD1306_BLACK);
          oled.drawPixel(x+1, y+1, SSD1306_BLACK);
        }
      }
    }
    oled.display();
    delay(30);
  }
  oled.clearDisplay();
  oled.display();
}

/*
 * displayClear
 * ------------
 * Clears the OLED display.
 */
void displayClear(Adafruit_SSD1306 &oled) {
  oled.clearDisplay();
  oled.display();
}
