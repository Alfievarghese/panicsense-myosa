/*
 * alerts.cpp — PanicSense Alert System Implementation
 * ====================================================
 * Implements WiFi connection, NTP time sync, HTTP POST alerts
 * with retry logic, BLE fallback for offline scenarios, and
 * SPIFFS circular episode logging.
 */

#include "alerts.h"

// ─── NTP Client ────────────────────────────────────────
static WiFiUDP ntpUDP;
static NTPClient timeClient(ntpUDP, NTP_SERVER, GMT_OFFSET_SEC, 60000); // update every 60s
static bool ntpInitialized = false;

// ─── BLE ───────────────────────────────────────────────
static BLEServer *bleServer = nullptr;
static BLECharacteristic *bleAlertCharacteristic = nullptr;
static bool bleInitialized = false;

// BLE UUIDs
#define BLE_SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// ─── SPIFFS ────────────────────────────────────────────
static bool spiffsReady = false;

/*
 * alertsWiFiConnect
 * -----------------
 * Attempts to connect to WiFi using credentials from config.h.
 * Times out after WIFI_CONNECT_TIMEOUT_MS.
 * Returns: true if connected, false if timed out.
 */
bool alertsWiFiConnect() {
  Serial.print(F("[ALERTS] Connecting to WiFi: "));
  Serial.println(WIFI_SSID);

  WiFi.disconnect(true);
  delay(100);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startAttempt >= WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println(F("[ALERTS] WiFi connection TIMED OUT"));
      return false;
    }
    delay(250); // Short delay during boot only — not in main loop
    Serial.print(F("."));
  }

  Serial.println();
  Serial.print(F("[ALERTS] WiFi connected. IP: "));
  Serial.println(WiFi.localIP());
  return true;
}

/*
 * alertsWiFiConnected
 * -------------------
 * Returns true if WiFi is currently connected.
 */
bool alertsWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

/*
 * alertsNTPInit
 * -------------
 * Initializes the NTP client and performs the first time sync.
 * Must be called after WiFi is connected.
 */
void alertsNTPInit() {
  timeClient.begin();
  timeClient.setTimeOffset(GMT_OFFSET_SEC);
  timeClient.update();
  ntpInitialized = true;
  Serial.print(F("[ALERTS] NTP initialized. Epoch: "));
  Serial.println(timeClient.getEpochTime());
}

/*
 * alertsGetEpoch
 * --------------
 * Returns the current Unix epoch timestamp from NTP.
 * Returns 0 if NTP has not been initialized or synced.
 */
unsigned long alertsGetEpoch() {
  if (!ntpInitialized) return 0;
  return timeClient.getEpochTime();
}

/*
 * alertsNTPUpdate
 * ---------------
 * Calls timeClient.update() to keep NTP time fresh.
 * Safe to call frequently — NTPClient internally throttles updates.
 */
void alertsNTPUpdate() {
  if (ntpInitialized) {
    timeClient.update();
  }
}

/*
 * alertsSPIFFSInit
 * ----------------
 * Initializes the SPIFFS filesystem for episode logging.
 * Returns true on success. On failure, logs error but does NOT crash.
 */
bool alertsSPIFFSInit() {
  if (!SPIFFS.begin(true)) { // true = format on first use
    Serial.println(F("[ALERTS] SPIFFS init FAILED — episodes will not be stored locally"));
    spiffsReady = false;
    return false;
  }
  spiffsReady = true;
  Serial.println(F("[ALERTS] SPIFFS init OK"));
  return true;
}

/*
 * alertsBLEInit
 * -------------
 * Initializes BLE server with a service and characteristic for
 * sending alert payloads to a connected phone app.
 * Used as fallback when WiFi HTTP POST fails.
 */
void alertsBLEInit() {
  BLEDevice::init("PanicSense");
  bleServer = BLEDevice::createServer();

  BLEService *service = bleServer->createService(BLE_SERVICE_UUID);

  bleAlertCharacteristic = service->createCharacteristic(
    BLE_CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );

  bleAlertCharacteristic->addDescriptor(new BLE2902());
  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  bleInitialized = true;
  Serial.println(F("[ALERTS] BLE initialized and advertising"));
}

/*
 * alertsBLESend
 * -------------
 * Writes a JSON payload string to the BLE characteristic and
 * notifies connected clients.
 * Input: payload — JSON string to send.
 */
void alertsBLESend(const String &payload) {
  if (!bleInitialized) {
    Serial.println(F("[ALERTS] Initializing BLE for fallback..."));
    alertsBLEInit();
  }

  if (bleAlertCharacteristic == nullptr) {
    Serial.println(F("[ALERTS] BLE failed to initialize — cannot send"));
    return;
  }

  bleAlertCharacteristic->setValue(payload.c_str());
  bleAlertCharacteristic->notify();
  Serial.println(F("[ALERTS] BLE notification sent"));
}

/*
 * alertsSendHTTP
 * --------------
 * Sends a JSON payload via HTTP POST to DASHBOARD_URL.
 * Retries up to HTTP_MAX_RETRIES times with HTTP_RETRY_DELAY_MS delay.
 *
 * If all retries fail and BLE is initialized, sends via BLE fallback.
 *
 * Input: payload — JSON string to POST.
 * Returns: true if any HTTP attempt succeeded (2xx response).
 */
bool alertsSendHTTP(const String &payload) {
  if (!alertsWiFiConnected()) {
    Serial.println(F("[ALERTS] WiFi not connected — attempting to reconnect..."));
    if (!alertsWiFiConnect()) {
      Serial.println(F("[ALERTS] Reconnect failed — falling back to BLE"));
      alertsBLESend(payload);
      return false;
    }
    // If it connected successfully here, ensure NTP is initialized
    alertsNTPInit();
  }

  Serial.printf("[ALERTS] Free Heap: %d, Max Block: %d\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  WiFiClientSecure *client = new WiFiClientSecure;
  if (!client) {
    Serial.println(F("[ALERTS] Failed to allocate WiFiClientSecure!"));
    return false;
  }
  client->setInsecure(); // Ignore SSL certificate validation (simplest for Vercel)
  
  HTTPClient http;
  http.setTimeout(15000); // 15 seconds timeout for slow SSL handshakes

  for (int attempt = 1; attempt <= HTTP_MAX_RETRIES; attempt++) {
    Serial.print(F("[ALERTS] HTTP POST attempt "));
    Serial.print(attempt);
    Serial.print(F("/"));
    Serial.println(HTTP_MAX_RETRIES);

    http.begin(*client, DASHBOARD_URL);
    http.addHeader("Content-Type", "application/json");

    int httpCode = http.POST(payload);

    if (httpCode >= 200 && httpCode < 300) {
      Serial.print(F("[ALERTS] HTTP POST success: "));
      Serial.println(httpCode);
      http.end();
      delete client;
      return true;
    }

    Serial.print(F("[ALERTS] HTTP POST failed: "));
    Serial.println(httpCode);
    if (httpCode < 0) {
        Serial.printf("[ALERTS] Error string: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();

    // Wait before retry (using delay — acceptable here as this runs
    // during EPISODE_ACTIVE after breathing is complete)
    if (attempt < HTTP_MAX_RETRIES) {
      unsigned long retryStart = millis();
      while (millis() - retryStart < HTTP_RETRY_DELAY_MS) {
        // Non-blocking wait
        yield();
      }
    }
  }

  // All retries failed — BLE fallback
  Serial.println(F("[ALERTS] All HTTP retries failed — BLE fallback"));
  alertsBLESend(payload);
  delete client;
  return false;
}

/*
 * alertsStoreEpisode
 * ------------------
 * Appends an episode JSON payload to the SPIFFS log file.
 * Implements circular log: max MAX_STORED_EPISODES entries.
 * When full, the oldest entry is removed.
 *
 * Storage format: one JSON object per line (JSON Lines format).
 *
 * Input: payload — JSON string to store.
 * Returns: true on successful write, false on error.
 */
bool alertsStoreEpisode(const String &payload) {
  if (!spiffsReady) {
    Serial.println(F("[ALERTS] SPIFFS not ready — episode not stored"));
    return false;
  }

  // Read existing episodes into memory
  String episodes[MAX_STORED_EPISODES];
  int episodeCount = 0;

  if (SPIFFS.exists(EPISODE_LOG_PATH)) {
    File readFile = SPIFFS.open(EPISODE_LOG_PATH, FILE_READ);
    if (readFile) {
      while (readFile.available() && episodeCount < MAX_STORED_EPISODES) {
        String line = readFile.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
          episodes[episodeCount] = line;
          episodeCount++;
        }
      }
      readFile.close();
    }
  }

  // If at capacity, shift all entries down by 1 (drop oldest)
  if (episodeCount >= MAX_STORED_EPISODES) {
    for (int i = 0; i < MAX_STORED_EPISODES - 1; i++) {
      episodes[i] = episodes[i + 1];
    }
    episodeCount = MAX_STORED_EPISODES - 1;
  }

  // Add new episode
  episodes[episodeCount] = payload;
  episodeCount++;

  // Write all episodes back to file
  File writeFile = SPIFFS.open(EPISODE_LOG_PATH, FILE_WRITE);
  if (!writeFile) {
    Serial.println(F("[ALERTS] Failed to open episode log for writing"));
    return false;
  }

  for (int i = 0; i < episodeCount; i++) {
    writeFile.println(episodes[i]);
  }
  writeFile.close();

  Serial.print(F("[ALERTS] Episode stored. Total: "));
  Serial.println(episodeCount);
  return true;
}

/*
 * alertsBuildPayload
 * ------------------
 * Constructs the JSON payload for an episode alert.
 *
 * Output format:
 * {
 *   "team": "MANDI MASALA",
 *   "event": "panic_episode",
 *   "timestamp": <epoch>,
 *   "bpm_estimate": <value>,
 *   "tremor_duration_ms": <value>,
 *   "pressure_hpa": <value>,
 *   "temperature_c": <value>,
 *   "trigger": "auto" or "manual"
 * }
 *
 * Inputs:
 *   bpmEstimate      — measured BPM (0 if not measured)
 *   tremorDurationMs — tremor duration in ms
 *   pressureHpa      — barometric pressure in hPa
 *   temperatureC     — temperature in Celsius
 *   triggerType      — "auto" or "manual"
 * Returns: serialized JSON string.
 */
String alertsBuildPayload(float bpmEstimate, unsigned long tremorDurationMs,
                          float pressureHpa, float temperatureC,
                          const char* triggerType) {
  StaticJsonDocument<512> doc;

  doc["team"] = "MANDI MASALA";
  doc["event"] = "panic_episode";
  doc["timestamp"] = alertsGetEpoch();
  doc["bpm_estimate"] = bpmEstimate;
  doc["tremor_duration_ms"] = tremorDurationMs;
  doc["pressure_hpa"] = pressureHpa;
  doc["temperature_c"] = temperatureC;
  doc["trigger"] = triggerType;

  String output;
  serializeJson(doc, output);

  Serial.print(F("[ALERTS] Payload: "));
  Serial.println(output);

  return output;
}
