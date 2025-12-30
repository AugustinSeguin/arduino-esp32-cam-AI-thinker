#include "app_start_ap_mode.h"
#include "config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "mbedtls/aes.h"
#include "mbedtls/md.h"

static WebServer *server = nullptr;
static DNSServer *dnsServer = nullptr;
static Preferences prefs;
static const char *PREF_NAMESPACE = "wifi_cfg";
static TaskHandle_t s_apServerTask = NULL;
static volatile bool s_apServerRunning = false;
static const byte DNS_PORT = 53;
static String csrfToken = "";
static bool isAuthenticated = false;
static unsigned long lastFailedAttempt = 0;
static int failedAttempts = 0;

// Macro pour les logs conditionnels
#define DEBUG_PRINT(x) if(DEBUG_MODE) Serial.print(x)
#define DEBUG_PRINTLN(x) if(DEBUG_MODE) Serial.println(x)
#define DEBUG_PRINTF(...) if(DEBUG_MODE) Serial.printf(__VA_ARGS__)

// Générer un token CSRF aléatoire
static String generateCSRFToken() {
  String token = "";
  for (int i = 0; i < 32; i++) {
    token += String(random(0, 16), HEX);
  }
  return token;
}

// Chiffrer une chaîne avec AES-128
static String encryptString(const String &plaintext) {
  mbedtls_aes_context aes;
  unsigned char key[16];
  memcpy(key, AES_KEY, 16);
  
  // Padding PKCS7
  int len = plaintext.length();
  int paddedLen = ((len / 16) + 1) * 16;
  unsigned char input[paddedLen];
  unsigned char output[paddedLen];
  
  memcpy(input, plaintext.c_str(), len);
  int padValue = paddedLen - len;
  for (int i = len; i < paddedLen; i++) {
    input[i] = padValue;
  }
  
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, 128);
  
  for (int i = 0; i < paddedLen; i += 16) {
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input + i, output + i);
  }
  
  mbedtls_aes_free(&aes);
  
  // Convertir en Base64 (simplifiée en hex pour l'exemple)
  String result = "";
  for (int i = 0; i < paddedLen; i++) {
    char buf[3];
    sprintf(buf, "%02x", output[i]);
    result += buf;
  }
  return result;
}

// Déchiffrer une chaîne avec AES-128
static String decryptString(const String &ciphertext) {
  if (ciphertext.length() % 2 != 0) return "";
  
  mbedtls_aes_context aes;
  unsigned char key[16];
  memcpy(key, AES_KEY, 16);
  
  int len = ciphertext.length() / 2;
  unsigned char input[len];
  unsigned char output[len];
  
  // Convertir hex en bytes
  for (int i = 0; i < len; i++) {
    sscanf(ciphertext.substring(i * 2, i * 2 + 2).c_str(), "%02x", (unsigned int*)&input[i]);
  }
  
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, key, 128);
  
  for (int i = 0; i < len; i += 16) {
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, input + i, output + i);
  }
  
  mbedtls_aes_free(&aes);
  
  // Retirer le padding PKCS7
  int padValue = output[len - 1];
  if (padValue > 0 && padValue <= 16) {
    len -= padValue;
  }
  
  return String((char*)output).substring(0, len);
}

// Valider le SSID
static bool isValidSSID(const String &ssid) {
  if (ssid.length() < 1 || ssid.length() > 32) return false;
  // Vérifier les caractères interdits
  for (unsigned int i = 0; i < ssid.length(); i++) {
    char c = ssid[i];
    if (c < 32 || c > 126) return false; // ASCII imprimable uniquement
  }
  return true;
}

// Valider le mot de passe WiFi
static bool isValidPassword(const String &password) {
  // WPA2 nécessite 8-63 caractères
  if (password.length() > 0 && (password.length() < 8 || password.length() > 63)) {
    return false;
  }
  return true;
}

bool loadSavedWiFi(String &outSsid, String &outPassword) {
  prefs.begin(PREF_NAMESPACE, true);
  String encSsid = prefs.getString("ssid_enc", "");
  String encPass = prefs.getString("pass_enc", "");
  prefs.end();
  
  if (encSsid.length() > 0) {
    outSsid = decryptString(encSsid);
    outPassword = decryptString(encPass);
    return outSsid.length() > 0;
  }
  return false;
}

static void handleLogin() {
  String page = "<!DOCTYPE html><html><head>";
  page += "<meta charset=\"utf-8\">";
  page += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  page += "<title>Connexion - ESP32-CAM</title>";
  page += "<style>body{font-family:Arial,sans-serif;margin:0;background:#f7f7f7;color:#222;}";
  page += ".wrap{max-width:400px;margin:20vh auto;padding:24px;background:#fff;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,0.08);}";
  page += "h2{margin:0 0 20px;color:#111;text-align:center;}";
  page += "label{display:block;margin:12px 0 6px;font-weight:600;}";
  page += "input[type=password]{width:100%;padding:12px;margin:4px 0 16px;border:1px solid #ccc;border-radius:6px;box-sizing:border-box;}";
  page += "input[type=submit]{width:100%;padding:14px;background:#007bff;color:#fff;border:none;border-radius:6px;font-size:16px;cursor:pointer;}";
  page += ".error{background:#ffe6e6;border:1px solid #ff4444;padding:10px;border-radius:6px;margin-bottom:16px;color:#cc0000;}";
  page += "</style></head><body>";
  page += "<div class=\"wrap\">";
  page += "<h2>🔒 Accès Sécurisé</h2>";
  
  if (failedAttempts >= 3 && (millis() - lastFailedAttempt) < 60000) {
    page += "<div class=\"error\">Trop de tentatives. Attendez 1 minute.</div>";
  }
  
  page += "<form method=\"POST\" action=\"/auth\">";
  page += "<label for=\"pwd\">Mot de passe</label>";
  page += "<input id=\"pwd\" name=\"pwd\" type=\"password\" placeholder=\"Mot de passe\" required autofocus>";
  page += "<input type=\"submit\" value=\"Se connecter\">";
  page += "</form>";
  page += "<p style=\"font-size:0.85em;color:#666;margin-top:20px;text-align:center;\">Mot de passe par défaut: <code>admin123</code></p>";
  page += "</div></body></html>";
  server->send(200, "text/html", page);
}

static void handleAuth() {
  // Rate limiting
  if (failedAttempts >= 3 && (millis() - lastFailedAttempt) < 60000) {
    server->send(429, "text/html", "<html><body><h3>Trop de tentatives. Attendez 1 minute.</h3></body></html>");
    return;
  }
  
  if (server->hasArg("pwd")) {
    String pwd = server->arg("pwd");
    if (pwd == PORTAL_PASSWORD) {
      isAuthenticated = true;
      csrfToken = generateCSRFToken();
      failedAttempts = 0;
      server->sendHeader("Location", "/");
      server->send(302);
      return;
    } else {
      failedAttempts++;
      lastFailedAttempt = millis();
      DEBUG_PRINTLN("Failed login attempt");
    }
  }
  
  server->sendHeader("Location", "/login?error=1");
  server->send(302);
}

static void handleRoot() {
  if (!isAuthenticated) {
    server->sendHeader("Location", "/login");
    server->send(302);
    return;
  }
  
  String page = "<!DOCTYPE html><html><head>";
  page += "<meta charset=\"utf-8\">";
  page += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  page += "<title>Configuration WiFi ESP32-CAM</title>";
  page += "<style>body{font-family:Arial,Helvetica,sans-serif;margin:0;background:#f7f7f7;color:#222;}";
  page += ".wrap{max-width:480px;margin:4vh auto;padding:18px;background:#fff;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,0.08);}";
  page += "h2{margin:0 0 12px 0;color:#111;}label{display:block;margin:8px 0 6px;font-weight:600;}";
  page += "input[type=text],input[type=password]{width:100%;padding:10px;margin:4px 0 12px;border:1px solid #ccc;border-radius:6px;box-sizing:border-box;}";
  page += "input[type=submit]{width:100%;padding:12px;background:#007bff;color:#fff;border:none;border-radius:6px;font-size:16px;cursor:pointer;}";
  page += "p.small{font-size:0.9em;color:#666;margin-top:8px;}";
  page += ".info{background:#e7f3ff;border:1px solid #2196F3;padding:10px;border-radius:6px;margin-bottom:12px;}";
  page += "@media (max-width:420px){.wrap{margin:3vh 12px;padding:14px}}";
  page += "</style></head><body>";

  page += "<div class=\"wrap\">";
  page += "<h2>🔧 Configuration Wi-Fi</h2>";
  page += "<div class=\"info\">⚠️ SSID: 1-32 caractères | Mot de passe: 8-63 caractères (WPA2)</div>";
  page += "<form method=\"POST\" action=\"/save\">";
  page += "<input type=\"hidden\" name=\"csrf\" value=\"" + csrfToken + "\">";
  page += "<label for=\"ssid\">Nom du Réseau (SSID)</label>";
  page += "<input id=\"ssid\" name=\"ssid\" type=\"text\" placeholder=\"Mon_WiFi\" maxlength=\"32\" required>";
  page += "<label for=\"pass\">Mot de Passe WiFi</label>";
  page += "<input id=\"pass\" name=\"pass\" type=\"password\" placeholder=\"Min 8 caractères\" minlength=\"8\" maxlength=\"63\">";
  page += "<input type=\"submit\" value=\"💾 Sauvegarder et Redémarrer\">";
  page += "</form>";
  page += "<p class=\"small\">Après la sauvegarde, l'appareil redémarrera et tentera de se connecter au réseau.</p>";
  page += "<hr style=\"border:none;border-top:1px solid #eee;margin:14px 0\">";
  page += "<p style=\"font-size:0.85em;color:#666;margin:0\">🔒 Connexion sécurisée | Auteur: <strong>Augustin SEGUIN</strong></p>";
  page += "</div></body></html>";
  server->send(200, "text/html", page);
}

static void handleSave() {
  if (!isAuthenticated) {
    server->send(403, "text/plain", "Forbidden");
    return;
  }
  
  // Vérification CSRF
  if (!server->hasArg("csrf") || server->arg("csrf") != csrfToken) {
    server->send(403, "text/html", "<html><body><h3>Erreur: Token CSRF invalide</h3><a href='/'>Retour</a></body></html>");
    DEBUG_PRINTLN("CSRF token mismatch");
    return;
  }
  
  if (server->hasArg("ssid")) {
    String ssid = server->arg("ssid");
    String pass = server->arg("pass");
    
    // Validation des entrées
    if (!isValidSSID(ssid)) {
      server->send(400, "text/html", "<html><body><h3>Erreur: SSID invalide (1-32 caractères ASCII)</h3><a href='/'>Retour</a></body></html>");
      DEBUG_PRINTLN("Invalid SSID");
      return;
    }
    
    if (!isValidPassword(pass)) {
      server->send(400, "text/html", "<html><body><h3>Erreur: Mot de passe invalide (8-63 caractères pour WPA2)</h3><a href='/'>Retour</a></body></html>");
      DEBUG_PRINTLN("Invalid password");
      return;
    }
    
    // Chiffrement et sauvegarde
    String encSsid = encryptString(ssid);
    String encPass = encryptString(pass);
    
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putString("ssid_enc", encSsid);
    prefs.putString("pass_enc", encPass);
    prefs.end();
    
    DEBUG_PRINTLN("WiFi credentials saved (encrypted)");

    String resp = "<html><head><meta charset='utf-8'></head><body>";
    resp += "<h3>✅ Configuration sauvegardée</h3>";
    resp += "<p>Redémarrage en cours...</p>";
    resp += "<p>L'appareil va se connecter à: <strong>" + ssid + "</strong></p>";
    resp += "</body></html>";
    server->send(200, "text/html", resp);

    delay(1000);
    server->stop();
    WiFi.softAPdisconnect(true);
    ESP.restart();
    return;
  }
  server->send(400, "text/plain", "Missing ssid");
}

static void handleNotFound() {
  // Pour le Captive Portal, rediriger vers login si pas authentifié
  if (!isAuthenticated) {
    server->sendHeader("Location", "/login");
    server->send(302);
  } else {
    handleRoot();
  }
}

void startConfigAP() {
  // Create AP with a predictable SSID but unique per device using MAC suffix
  String mac = WiFi.macAddress(); // "AA:BB:CC:DD:EE:FF"
  mac.replace(":", "");
  String suffix = mac.substring(mac.length() - 6); // last 6 hex chars
  String apName = "ESP32-CAM-" + suffix;
  DEBUG_PRINTF("Starting config AP: %s\n", apName.c_str());

  WiFi.mode(WIFI_AP);
  // Ajouter un mot de passe WPA2 au point d'accès
  WiFi.softAP(apName.c_str(), AP_PASSWORD);
  delay(500);

  IPAddress ip = WiFi.softAPIP();
  DEBUG_PRINTF("AP IP: %s\n", ip.toString().c_str());
  DEBUG_PRINTF("AP Password: %s\n", AP_PASSWORD);

  // Démarrer le serveur DNS pour le Captive Portal
  if (!dnsServer) {
    dnsServer = new DNSServer();
    // Rediriger toutes les requêtes DNS vers l'IP de l'AP (192.168.4.1)
    dnsServer->start(DNS_PORT, "*", ip);
    DEBUG_PRINTLN("DNS Server started for Captive Portal");
  }

  // Initialiser l'état d'authentification
  isAuthenticated = false;
  csrfToken = "";
  failedAttempts = 0;

  if (!server) {
    server = new WebServer(80);
  }
  server->on("/", HTTP_GET, handleRoot);
  server->on("/login", HTTP_GET, handleLogin);
  server->on("/auth", HTTP_POST, handleAuth);
  server->on("/save", HTTP_POST, handleSave);
  // Routes supplémentaires pour le Captive Portal
  server->on("/generate_204", HTTP_GET, handleLogin); // Android
  server->on("/fwlink", HTTP_GET, handleLogin);       // Microsoft
  server->on("/hotspot-detect.html", HTTP_GET, handleLogin); // Apple
  server->onNotFound(handleNotFound);
  server->begin();
  
  // Start a small task that will call handleClient() periodically so the server responds
  if (s_apServerTask == NULL) {
    s_apServerRunning = true;
    xTaskCreatePinnedToCore([](void*){
      while (s_apServerRunning) {
        if (dnsServer) dnsServer->processNextRequest();
        if (server) server->handleClient();
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      s_apServerTask = NULL;
      vTaskDelete(NULL);
    }, "ap_server", 4096, NULL, 1, &s_apServerTask, 1);
  }

  DEBUG_PRINTLN("Captive Portal started with authentication.");
  DEBUG_PRINTF("Connect to '%s' with password '%s'\n", apName.c_str(), AP_PASSWORD);
  DEBUG_PRINTF("Portal password: %s\n", PORTAL_PASSWORD);
}

void stopConfigAP() {
  // Stop the server task first
  s_apServerRunning = false;
  // give the task a moment to exit
  uint32_t t = 0;
  while (s_apServerTask && (t++ < 50)) {
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  if (dnsServer) {
    dnsServer->stop();
    delete dnsServer;
    dnsServer = nullptr;
  }
  if (server) {
    server->stop();
    delete server;
    server = nullptr;
  }
  WiFi.softAPdisconnect(true);
  isAuthenticated = false;
  csrfToken = "";
  DEBUG_PRINTLN("Config AP stopped");
}
