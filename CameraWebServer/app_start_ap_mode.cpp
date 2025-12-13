#include "app_start_ap_mode.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

static WebServer *server = nullptr;
static Preferences prefs;
static const char *PREF_NAMESPACE = "wifi_cfg";
static TaskHandle_t s_apServerTask = NULL;
static volatile bool s_apServerRunning = false;

bool loadSavedWiFi(String &outSsid, String &outPassword) {
  prefs.begin(PREF_NAMESPACE, true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();
  if (ssid.length() > 0) {
    outSsid = ssid;
    outPassword = pass;
    return true;
  }
  return false;
}

static void handleRoot() {
  String page = "<!DOCTYPE html><html><head>";
  page += "<meta charset=\"utf-8\">";
  page += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  page += "<title>Configuration WiFi ESP32-CAM</title>"; // Traduction du titre
  page += "<style>body{font-family:Arial,Helvetica,sans-serif;margin:0;background:#f7f7f7;color:#222;}";
  page += ".wrap{max-width:480px;margin:4vh auto;padding:18px;background:#fff;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,0.08);}";
  page += "h2{margin:0 0 12px 0;color:#111;}label{display:block;margin:8px 0 6px;font-weight:600;}";
  page += "input[type=text],input[type=password]{width:100%;padding:10px;margin:4px 0 12px;border:1px solid #ccc;border-radius:6px;box-sizing:border-box;}";
  page += "input[type=submit]{width:100%;padding:12px;background:#007bff;color:#fff;border:none;border-radius:6px;font-size:16px;cursor:pointer;}"; // Ajout de cursor:pointer
  page += "p.small{font-size:0.9em;color:#666;margin-top:8px;}";
  page += "@media (max-width:420px){.wrap{margin:3vh 12px;padding:14px}}";
  page += "</style></head><body>";

  page += "<div class=\"wrap\">";
  page += "<h2>Configuration Wi-Fi</h2>"; // Traduction
  page += "<form method=\"POST\" action=\"/save\">";
  page += "<label for=\"ssid\">Nom du Réseau (SSID)</label><input id=\"ssid\" name=\"ssid\" type=\"text\" placeholder=\"Mon_WiFi_Maison\">"; // Traduction et placeholder français
  page += "<label for=\"pass\">Mot de Passe</label><input id=\"pass\" name=\"pass\" type=\"password\" placeholder=\"Mot de Passe\">"; // Traduction et placeholder français
  page += "<input type=\"submit\" value=\"Sauvegarder et Connecter\">"; // Traduction du bouton
  page += "</form>";
  page += "<p class=\"small\">Après la sauvegarde, l'appareil tentera de se connecter au réseau spécifié et redémarrera.</p>"; // Traduction du texte d'instruction
  page += "<hr style=\"border:none;border-top:1px solid #eee;margin:14px 0\">";
  page += "<p style=\"font-size:0.85em;color:#666;margin:0\">Auteur: <strong>Augustin SEGUIN</strong></p>";
  page += "</div></body></html>";
                        server->send(200, "text/html", page);
}

static void handleSave() {
  if (server->hasArg("ssid")) {
    String ssid = server->arg("ssid");
    String pass = server->arg("pass");
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();

    String resp = "<html><body><h3>Saved. Rebooting to apply settings...</h3></body></html>";
    server->send(200, "text/html", resp);

    delay(500);
    // Stop server and reboot to let setup continue with new creds
    server->stop();
    WiFi.softAPdisconnect(true);
    ESP.restart();
    return;
  }
  server->send(400, "text/plain", "Missing ssid");
}

static void handleNotFound() {
  server->send(404, "text/plain", "Not found");
}

void startConfigAP() {
  // Create AP with a predictable SSID but unique per device using MAC suffix
  String mac = WiFi.macAddress(); // "AA:BB:CC:DD:EE:FF"
  mac.replace(":", "");
  String suffix = mac.substring(mac.length() - 6); // last 6 hex chars
  String apName = "ESP32-CAM-" + suffix;
  Serial.printf("Starting config AP: %s\n", apName.c_str());

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName.c_str());
  delay(500);

  IPAddress ip = WiFi.softAPIP();
  Serial.printf("AP IP: %s\n", ip.toString().c_str());

  if (!server) {
    server = new WebServer(80);
  }
  server->on("/", HTTP_GET, handleRoot);
  server->on("/save", HTTP_POST, handleSave);
  server->onNotFound(handleNotFound);
  server->begin();
  // Start a small task that will call handleClient() periodically so the server responds
  if (s_apServerTask == NULL) {
    s_apServerRunning = true;
    xTaskCreatePinnedToCore([](void*){
      while (s_apServerRunning) {
        if (server) server->handleClient();
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      s_apServerTask = NULL;
      vTaskDelete(NULL);
    }, "ap_server", 3072, NULL, 1, &s_apServerTask, 1);
  }

  Serial.println("Config AP web server started. Connect to the AP and open http://192.168.4.1/");
}

void stopConfigAP() {
  // Stop the server task first
  s_apServerRunning = false;
  // give the task a moment to exit
  uint32_t t = 0;
  while (s_apServerTask && (t++ < 50)) {
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  if (server) {
    server->stop();
    delete server;
    server = nullptr;
  }
  WiFi.softAPdisconnect(true);
  Serial.println("Config AP stopped");
}
