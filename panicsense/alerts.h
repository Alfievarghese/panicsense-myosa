/*
 * alerts.h — PanicSense Alert System Interface
 * ==============================================
 * Handles WiFi HTTP POST alerts to the dashboard, BLE fallback,
 * SPIFFS episode logging, and NTP time synchronization.
 */

#ifndef ALERTS_H
#define ALERTS_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPIFFS.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "config.h"

// ─── WiFi ──────────────────────────────────────────────
// Connects to WiFi with timeout. Returns true if connected within
// WIFI_CONNECT_TIMEOUT_MS, false if timed out (device enters local mode).
bool alertsWiFiConnect();

// Returns true if WiFi is currently connected.
bool alertsWiFiConnected();

// ─── NTP ───────────────────────────────────────────────
// Initializes NTP client and performs initial time sync.
// Must be called after WiFi is connected.
void alertsNTPInit();

// Returns current epoch timestamp from NTP.
// Returns 0 if NTP has not been synced.
unsigned long alertsGetEpoch();

// Updates NTP time. Call periodically from loop().
void alertsNTPUpdate();

// ─── SPIFFS ────────────────────────────────────────────
// Initializes SPIFFS filesystem. Returns true on success.
// On failure, logs error and continues — episodes won't be stored locally.
bool alertsSPIFFSInit();

// ─── BLE ───────────────────────────────────────────────
// Initializes BLE server with a characteristic for alert payloads.
// Used as fallback when WiFi POST fails.
void alertsBLEInit();

// Writes a JSON payload string to the BLE characteristic and notifies
// connected clients.
void alertsBLESend(const String &payload);

// ─── HTTP POST Alert ───────────────────────────────────
// Sends an episode alert as HTTP POST to DASHBOARD_URL.
// Retries up to HTTP_MAX_RETRIES times with HTTP_RETRY_DELAY_MS between.
// Returns true if any attempt succeeds (HTTP 200-299).
// If all retries fail, automatically triggers BLE fallback.
// Input: payload — JSON string to POST.
bool alertsSendHTTP(const String &payload);

// ─── SPIFFS Episode Log ────────────────────────────────
// Stores an episode JSON payload to SPIFFS as a circular log.
// Max MAX_STORED_EPISODES entries; oldest deleted on overflow.
// Input: payload — JSON string to store.
// Returns true on successful write, false on error.
bool alertsStoreEpisode(const String &payload);

// ─── Build Episode Payload ─────────────────────────────
// Constructs the JSON payload for an episode alert.
// Inputs:
//   bpmEstimate      — measured BPM (0 if pulse not measured)
//   tremorDurationMs — how long the tremor lasted in ms
//   pressureHpa      — BMP180 barometric pressure in hPa
//   temperatureC     — BMP180 temperature in Celsius
//   triggerType      — "auto" or "manual" (gesture SOS)
// Returns: JSON string ready for HTTP POST and SPIFFS storage.
String alertsBuildPayload(float bpmEstimate, unsigned long tremorDurationMs,
                          float pressureHpa, float temperatureC,
                          const char* triggerType);

#endif // ALERTS_H
