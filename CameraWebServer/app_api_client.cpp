#include <ArduinoJson.h>
#include "app_api_client.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <Arduino.h>

// server upload variables defined in main sketch
#include "config.h"

// Utiliser les constantes de config.h pour les notifs
#define SERVER_API_URL SERVER_API_URL_CONFIG
#define API_KEY API_KEY_CONFIG
#define CAMERA_KEY CAMERA_KEY_CONFIG

// Envoie un heartbeat (cam_key) pour mettre à jour l'IP sur le backend
void updateCameraHeartbeatToServer() {
  if (!SERVER_API_URL || strlen(SERVER_API_URL) == 0) return;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[API] WiFi not connected — skipping heartbeat");
    return;
  }
  HTTPClient http;
  String url = String(SERVER_API_URL);
  if (!url.endsWith("/")) url += "/";
  url += "cameras/update-ip";
  http.setTimeout(5000);
  bool begun = false;
  if (url.startsWith("https://")) {
    WiFiClientSecure *client = new WiFiClientSecure();
    client->setInsecure();
    begun = http.begin(*client, url);
  } else {
    begun = http.begin(url);
  }
  if (!begun) {
    Serial.println("[API] http.begin failed (heartbeat)");
    return;
  }
  // Prépare le JSON {"cam_key":"..."}
  StaticJsonDocument<128> doc;
  doc["cam_key"] = CAMERA_KEY;
  doc["local_ip"] = WiFi.localIP().toString();
  String payload;
  serializeJson(doc, payload);
  http.addHeader("Content-Type", "application/json");
  if (API_KEY && strlen(API_KEY) > 0) http.addHeader("X-API-Key", API_KEY);
  if (CAMERA_API_KEY && strlen(CAMERA_API_KEY) > 0) http.addHeader("X-Camera-API-Key", CAMERA_API_KEY);
  int code = http.POST((uint8_t*)payload.c_str(), payload.length());
  if (code > 0) {
    Serial.printf("[API] Heartbeat POST %s returned %d\n", url.c_str(), code);
  } else {
    Serial.printf("[API] Heartbeat POST %s failed: %d\n", url.c_str(), code);
  }
  http.end();
}

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
  if (!url.endsWith("/")) url += "/";
  url += "cameras/notification/";
  if (CAMERA_KEY && strlen(CAMERA_KEY) > 0) {
    url += CAMERA_KEY;
  }
  http.setTimeout(5000);
  bool begun = false;
  if (url.startsWith("https://")) {
    WiFiClientSecure *client = new WiFiClientSecure();
    client->setInsecure(); // accept all certs for ngrok //todo to remove
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
  if (CAMERA_API_KEY && strlen(CAMERA_API_KEY) > 0) http.addHeader("X-Camera-API-Key", CAMERA_API_KEY);

  int code = http.POST((uint8_t*)payload, payloadLen);
  if (code > 0) {
    Serial.printf("[API] POST %s returned %d\n", url.c_str(), code);
  } else {
    Serial.printf("[API] POST %s failed: %d\n", url.c_str(), code);
  }
  http.end();
}
