#ifndef CONFIG_H
#define CONFIG_H


// WiFi Credentials
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Server API Configuration
#define SERVER_API_URL_CONFIG ""
#define API_KEY_CONFIG ""
#define CAMERA_KEY_CONFIG ""

// Image save interval (seconds)
#define SAVE_INTERVAL 5

// Security Configuration
#define AP_PASSWORD ""  // Mot de passe du point d'accès (min 8 caractères pour WPA2)
#define PORTAL_PASSWORD ""  // Mot de passe pour accéder au portail web
#define AES_KEY ""  // Clé AES-128 (16 caractères) pour chiffrement
#define DEBUG_MODE false            // false en production pour désactiver les logs

#endif // CONFIG_H

