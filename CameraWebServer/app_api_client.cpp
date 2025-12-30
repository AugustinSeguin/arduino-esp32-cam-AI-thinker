#include "app_api_client.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <Arduino.h>

// server upload variables defined in main sketch
extern const char *SERVER_API_URL;
extern const char *API_KEY;
extern const char *CAMERA_KEY;

// Send a motion notification POST to SERVER_API_URL/<cameraKey>
void notifyMotionToServer(const char* cameraKey) {
  if (!SERVER_API_URL || strlen(SERVER_API_URL) == 0) return;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[API] WiFi not connected — skipping motion notify");
    return;
  }
  HTTPClient http;
  // Build URL: https://<ngrok>/cameras/notification/<cam_esp32_001>
  String url = String(SERVER_API_URL);
  if (cameraKey && strlen(cameraKey) > 0) {
    if (!url.endsWith("/")) url += "/";
    url += cameraKey;
  }
  http.setTimeout(5000);
  bool begun = false;
  if (url.startsWith("https://")) {
    WiFiClientSecure *client = new WiFiClientSecure();
    client->setInsecure(); // accept all certs for ngrok
    begun = http.begin(*client, url);
  } else {
    begun = http.begin(url);
  }
  if (!begun) {
    Serial.println("[API] http.begin failed");
    return;
  }
  
  // Build JSON payload matching the API: {type, message, timestamp}
  time_t now = time(nullptr);
  char payload[256];
  if (now > 1000000000UL) {
    snprintf(payload, sizeof(payload), 
             "{\"type\":\"motion\",\"message\":\"Mouvement détecté par la caméra\",\"timestamp\":%lu}", 
             (unsigned long)now);
  } else {
    unsigned long t = (unsigned long)(millis() / 1000UL);
    snprintf(payload, sizeof(payload), 
             "{\"type\":\"motion\",\"message\":\"Mouvement détecté (uptime: %lus)\"}", 
             t);
  }

  // Add headers AFTER building payload (need Content-Length)
  http.addHeader("Content-Type", "application/json");
  size_t payloadLen = strlen(payload);
  char contentLenStr[16];
  snprintf(contentLenStr, sizeof(contentLenStr), "%zu", payloadLen);
  http.addHeader("Content-Length", contentLenStr);
  if (API_KEY && strlen(API_KEY) > 0) http.addHeader("X-API-Key", API_KEY);

  int code = http.POST((uint8_t*)payload, payloadLen);
  if (code > 0) {
    Serial.printf("[API] POST %s returned %d\n", url.c_str(), code);
  } else {
    Serial.printf("[API] POST %s failed: %d\n", url.c_str(), code);
  }
  http.end();
}
