#ifndef APP_SD_MANAGER_H
#define APP_SD_MANAGER_H

#include <stdint.h>

// Start a background task that captures and saves images to SD card
// every intervalSeconds seconds, with motion detection
void startImageSaverTask(uint32_t intervalSeconds = 2);

// Stop the background task (if running)
void stopImageSaverTask();

// Initialize SD subsystem (returns true if mounted)
bool sdInit();

// Remove all saved images from the SD (folder /photos)
void clearSavedImages();

#endif // APP_SD_MANAGER_H
