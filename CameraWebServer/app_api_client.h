#ifndef APP_API_CLIENT_H
#define APP_API_CLIENT_H

#include <stdint.h>

// Send a motion notification POST to the configured server API
// Requires WiFi connection and valid SERVER_API_URL, API_KEY, CAMERA_KEY
void notifyMotionToServer(const char* cameraKey);

#endif // APP_API_CLIENT_H
