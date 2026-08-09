# PanicSense

🕒 August 2026 — PanicSense

A wrist-worn device that tries to catch a panic attack while it's happening, not after.

---

## Overview

The idea started from a simple frustration: every panic-attack wearable I looked at assumes you can still use a phone when you're mid-episode. You can't, really. Your hands are shaking, your thoughts are somewhere else entirely, and the last thing you want to do is unlock a screen and tap through an app.

PanicSense is built on the MYOSA Mini IoT Kit and tries to skip that step. The MPU6050 watches for sustained hand tremor. If it sees something that isn't just a bump or a shaky bus ride, the device asks you to rest a finger on the APDS9960 sensor for a few seconds — which, admittedly, isn't a sensor built for this at all. It's meant for gestures and ambient light. We're using its IR LED and photodiode the same way a smartwatch camera reads a pulse, just repurposed on hardware that was never designed for it.

Only when both signals agree — tremor and an elevated pulse — does anything happen. That was a deliberate choice after reading how often single-sensor wearables cry wolf over cold hands or a shaky commute. If the pulse reading times out or comes back inconclusive, the device just quietly resets. No alert, no drama.

When it does confirm an episode, the OLED switches into a guided breathing pattern, the buzzer gives three short beeps so you know something happened without having to look, and an alert goes out over WiFi to a small dashboard we built alongside it. A BMP180 tags each episode with pressure and temperature, mostly because there's real research linking pressure drops to anxiety, and it felt worth logging even if we haven't collected enough episodes yet to see a pattern.

Everything that matters — the breathing pacer, the alert — runs on-device. If WiFi drops, the wearable doesn't just stop working; it falls back to BLE. The Gemini layer that turns raw sensor numbers into a readable message for a trusted contact is a nice-to-have on top, not something the core safety path depends on.

---

## What Actually Works Right Now

Being honest about where this stands: tremor detection is solid. Shake the MPU6050 hard enough for about 300ms and it reliably kicks into the confirming state — the false-alarm timeout also works, so a random bump doesn't escalate into anything.

Pulse confirmation is the rougher edge. The APDS9960 isn't a real PPG sensor, and getting a clean BPM reading depends a lot on finger pressure and how still you hold it. In our testing it sometimes returns 0 BPM instead of a real number, which just means the peak-detection algorithm couldn't find a clean signal that round, not that the pulse was zero. We're treating this as an experimental layer, not something to lean on for accuracy, and we say so plainly instead of pretending it's medical-grade.

---

## Demo

<table>
<tr>
<td width="50%">
<img src="assets/photo-01-full-setup.jpg" width="100%"/>
<p align="center"><sub>Full setup — MPU6050, APDS9960, BMP180, OLED, and buzzer daisy-chained off the MYOSA motherboard</sub></p>
</td>
<td width="50%">
<img src="assets/photo-03-oled-boot.jpg" width="100%"/>
<p align="center"><sub>Boot splash — Team MANDI MASALA</sub></p>
</td>
</tr>
<tr>
<td width="50%">
<img src="assets/photo-02-oled-idle.jpg" width="100%"/>
<p align="center"><sub>Idle state, watching for tremor</sub></p>
</td>
<td width="50%">
<img src="assets/photo-06-oled-confirming.jpg" width="100%"/>
<p align="center"><sub>Confirming — reading pulse, 16 seconds left on the window</sub></p>
</td>
</tr>
<tr>
<td width="50%">
<img src="assets/photo-07-pulse-detection.jpg" width="100%"/>
<p align="center"><sub>Finger placed on the APDS9960 window during pulse confirmation</sub></p>
</td>
<td width="50%">
<img src="assets/photo-05-oled-breathing.jpg" width="100%"/>
<p align="center"><sub>Breathing pacer, cycle 1 of 3</sub></p>
</td>
</tr>
<tr>
<td width="50%">
<img src="assets/photo-04-oled-cooldown.jpg" width="100%"/>
<p align="center"><sub>Alert sent, five-minute cooldown before it can trigger again</sub></p>
</td>
<td width="50%">
<img src="assets/photo-08-dashboard.jpg" width="100%"/>
<p align="center"><sub>Live dashboard — episode feed with BPM, duration, and temperature</sub></p>
</td>
</tr>
</table>

### Video

[panicsense-demo.mp4](panicsense-demo.mp4) — full walkthrough from idle, through tremor and pulse confirmation, to the breathing pacer and alert.

---

## How Each Piece Works

**Tremor detection.** The MPU6050 is read every 100ms. A rolling window of the last 10 readings feeds a variance calculation across all three axes. Tremor only counts once that variance clears the threshold for three windows in a row — about 300ms of sustained shaking, not a single spike. That one change cut out almost all the false triggers we were getting early on from just tapping the desk near the board.

**Pulse confirmation.** This is the part I keep going back to. The APDS9960's proximity register was never built to read a pulse, but a finger pressed over its IR LED modulates the returned signal in roughly the same way a phone camera flash does. We sample that for up to 20 seconds, run it through a peak detector, and estimate BPM from the interval between peaks. It's noisy. It works often enough to be useful as a confirmation layer, not often enough to trust on its own, which is exactly why it's paired with tremor rather than standing alone.

**Manual override.** If someone's already aware something's wrong and just wants to trigger the alert directly, a swipe over the same APDS9960 in gesture mode does it instantly, skipping the confirmation window entirely.

**Breathing pacer.** Once an episode is confirmed, the OLED runs three cycles of box breathing — four seconds each for inhale, hold, exhale, hold. A small circle expands and contracts with each phase so it's something to actually follow, not just text on a screen.

**Alert and dashboard.** The ESP32 posts a JSON payload — timestamp, BPM estimate, tremor duration, pressure, temperature — to a small React dashboard over WiFi. If that fails, it retries a few times before falling back to BLE. The dashboard shows a running feed of episodes and a live line that moves whenever new data comes in. It's simple on purpose; there wasn't a lot of time to make it more than that.

**Episode logging.** Up to 50 episodes get written to SPIFFS on the device itself, so they survive a reboot and sync up whenever the dashboard reconnects.

---

## Repo Structure

```
panicsense-myosa/
├── panicsense-myosa.md      # this file
├── panicsense-cover.jpg     # cover image
├── panicsense-demo.mp4      # demo video
├── assets/                  # all photos used above
│   ├── photo-01-full-setup.jpg
│   ├── photo-02-oled-idle.jpg
│   ├── photo-03-oled-boot.jpg
│   ├── photo-04-oled-cooldown.jpg
│   ├── photo-05-oled-breathing.jpg
│   ├── photo-06-oled-confirming.jpg
│   ├── photo-07-pulse-detection.jpg
│   └── photo-08-dashboard.jpg
├── panicsense/               # ESP32 firmware (Arduino)
│   ├── panicsense.ino        # state machine, setup(), loop()
│   ├── config.h              # pins, thresholds, WiFi/dashboard config
│   ├── tremor.h / tremor.cpp     # MPU6050 tremor detection
│   ├── pulse.h / pulse.cpp       # APDS9960 pulse + gesture logic
│   ├── breathing.h / breathing.cpp  # box breathing pacer
│   ├── display.h / display.cpp      # OLED screens for all states
│   └── alerts.h / alerts.cpp        # WiFi POST, BLE fallback, SPIFFS, NTP
└── dashboard/                # React (Next.js) live dashboard
    ├── app/                  # pages and API routes
    ├── public/
    ├── package.json
    └── next.config.ts
```

---

## Setting It Up

1. Daisy-chain the sensors off the motherboard in this order: MPU6050 → APDS9960 → BMP180 → OLED, using the JST cables included in the kit.
2. Wire the buzzer: GND to GND, 5V to VIN, SIG to GPIO 26. (Avoid GPIO 4 and GPIO 12 on this board — both are strapping pins and can prevent the ESP32 from booting if pulled high.)
3. Open `panicsense/config.h` and set your WiFi SSID, password, and dashboard URL.
4. In Arduino IDE, select **ESP32 Dev Module** as the board and **Huge APP (3MB No OTA / 1MB SPIFFS)** as the partition scheme. The sketch is too large for the default partition once BLE and WiFi are both compiled in.
5. Install the libraries listed below, then upload. If the upload fails with a boot-mode error, hold BOOT, tap EN/RESET once while still holding BOOT, then start the upload and release BOOT once it says "Connecting."
6. Start the dashboard with `npm install && npm run dev` inside the `dashboard` folder.
7. Watch Serial Monitor at 115200 baud on first boot to confirm all four sensors initialize.

---

## Tech Stack

- MYOSA ESP32 motherboard — WiFi, BLE, I2C
- MPU6050 — tremor detection
- APDS9960 — gesture override and repurposed pulse sensing
- BMP180 — pressure and temperature logging
- SSD1306 OLED — display and breathing pacer
- Active buzzer on GPIO 26
- Arduino C++ firmware
- React + Next.js dashboard
- SPIFFS for on-device episode storage
- NTP for real timestamps
- Gemini API for optional message enrichment

---

## Libraries

```
Adafruit SSD1306
Adafruit GFX Library
Adafruit MPU6050
Adafruit Unified Sensor
Adafruit BMP085 Unified
SparkFun APDS9960 RGB and Gesture Sensor
NTPClient (Fabrice Weinberg)
ArduinoJson (Benoit Blanchon)
```

---

## A Note on Accuracy

The pulse reading through the APDS9960 is experimental. It's a real physical technique, but this isn't the sensor anyone would choose for it if biometric accuracy were the goal. We're upfront about that here and in the firmware comments. PanicSense is a prototype and a class project, not a medical device, and nothing in it should be read as a clinical claim.

---

## Team

MANDI MASALA — St. Joseph's College of Engineering and Technology, Palai, Kerala
IEEE MYOSA International Event 6.0, IEEE Sensors Conference 2026
