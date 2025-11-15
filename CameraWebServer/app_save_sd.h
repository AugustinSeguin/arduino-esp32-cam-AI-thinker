#ifndef APP_SAVE_SD_H
#define APP_SAVE_SD_H

#include <stdint.h>

// Start a background task that captures an image and saves it to the SD card
// every intervalSeconds seconds. If the SD is not present the task will keep
// retrying and log an error. Default interval is 2 seconds.
void startImageSaverTask(uint32_t intervalSeconds = 2);

// Stop the background task (if running)
void stopImageSaverTask();

// Initialize SD subsystem (returns true if mounted)
bool sdInit();

// Remove all saved images from the SD (folder /photos)
void clearSavedImages();

#endif // APP_SAVE_SD_H
