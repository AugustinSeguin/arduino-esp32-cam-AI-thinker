#ifndef APP_START_AP_MODE_H
#define APP_START_AP_MODE_H

#include <Arduino.h>

// Try to load saved WiFi credentials from non-volatile storage.
// Returns true if both SSID and password were found.
bool loadSavedWiFi(String &outSsid, String &outPassword);

// Start the configuration Access Point and HTTP server.
// The AP serves a simple form to set SSID and password which are saved in NVS.
void startConfigAP();

// Stop the configuration AP and its HTTP server.
void stopConfigAP();

#endif // APP_START_AP_MODE_H
