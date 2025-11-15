#include "app_save_sd.h"
#include "esp_camera.h"
#include <SD_MMC.h>
#include <FS.h>
#include <Arduino.h>

static TaskHandle_t s_saveTaskHandle = NULL;
static volatile bool s_taskRunning = false;

// Try to initialize SD_MMC and return whether it succeeded
bool sdInit() {
  if (SD_MMC.begin()) {
    Serial.println("SD_MMC mounted successfully");
    return true;
  } else {
    Serial.println("SD_MMC mount failed");
    return false;
  }
}

// Utility: create /photos folder if missing
static void ensurePhotosDir() {
  if (!SD_MMC.exists("/photos")) {
    SD_MMC.mkdir("/photos");
  }
}

// Task: capture and save images every intervalSeconds seconds
static void imageSaveTask(void* pv) {
  uint32_t intervalSeconds = 2;
  if (pv) {
    uint32_t *p = (uint32_t*)pv;
    intervalSeconds = *p;
    free(p);
  }
  s_taskRunning = true;
  uint32_t counter = 0;

  while (s_taskRunning) {
    if (!sdInit()) {
      Serial.println("[ImageSaver] SD not available, retrying later...");
      vTaskDelay(pdMS_TO_TICKS(intervalSeconds * 1000UL));
      continue;
    }

    ensurePhotosDir();

    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[ImageSaver] Camera capture failed");
      vTaskDelay(pdMS_TO_TICKS(intervalSeconds * 1000UL));
      continue;
    }

    // Only save JPEG frames
    if (fb->format != PIXFORMAT_JPEG) {
      Serial.println("[ImageSaver] Frame not in JPEG format — skipping save");
      esp_camera_fb_return(fb);
      vTaskDelay(pdMS_TO_TICKS(intervalSeconds * 1000UL));
      continue;
    }

    // Build filename using millis + counter to avoid collisions
    char path[64];
    uint32_t t = (uint32_t)(millis() / 1000UL);
    snprintf(path, sizeof(path), "/photos/IMG_%lu_%u.jpg", t, counter++);

    File file = SD_MMC.open(path, FILE_WRITE);
    if (!file) {
      Serial.printf("[ImageSaver] Failed to open file for writing: %s\n", path);
      esp_camera_fb_return(fb);
      vTaskDelay(pdMS_TO_TICKS(intervalSeconds * 1000UL));
      continue;
    }

    size_t written = file.write(fb->buf, fb->len);
    file.close();

    if (written == fb->len) {
      Serial.printf("[ImageSaver] Saved %u bytes to %s\n", (unsigned)written, path);
    } else {
      Serial.printf("[ImageSaver] Incomplete write %u/%u bytes to %s\n", (unsigned)written, (unsigned)fb->len, path);
    }

    esp_camera_fb_return(fb);

    vTaskDelay(pdMS_TO_TICKS(intervalSeconds * 1000UL));
  }

  s_saveTaskHandle = NULL;
  vTaskDelete(NULL);
}

void startImageSaverTask(uint32_t intervalSeconds) {
  if (s_saveTaskHandle != NULL) {
    Serial.println("[ImageSaver] Task already running");
    return;
  }
  // pass interval via heap to task
  uint32_t *p = (uint32_t*)malloc(sizeof(uint32_t));
  if (!p) return;
  *p = intervalSeconds;

  // Create task on core 1 if possible
  xTaskCreatePinnedToCore(imageSaveTask, "image_saver", 4096, p, 1, &s_saveTaskHandle, 1);
  Serial.printf("[ImageSaver] Started task with interval %us\n", (unsigned)intervalSeconds);
}

void stopImageSaverTask() {
  if (!s_saveTaskHandle) {
    Serial.println("[ImageSaver] Task not running");
    return;
  }
  s_taskRunning = false;
  // Give the task a little time to exit
  uint32_t t = 0;
  while (s_saveTaskHandle && (t++ < 20)) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  if (s_saveTaskHandle) {
    vTaskDelete(s_saveTaskHandle);
    s_saveTaskHandle = NULL;
  }
  Serial.println("[ImageSaver] Stopped task");
}

void clearSavedImages() {
  if (!sdInit()) {
    Serial.println("[ImageSaver] SD not available — cannot clear images");
    return;
  }
  if (!SD_MMC.exists("/photos")) {
    Serial.println("[ImageSaver] /photos does not exist — nothing to clear");
    return;
  }

  File root = SD_MMC.open("/photos");
  if (!root) {
    Serial.println("[ImageSaver] Failed to open /photos");
    return;
  }

  File file = root.openNextFile();
  int removed = 0;
  while (file) {
    String name = file.name();
    file.close();
    if (SD_MMC.remove(name)) {
      removed++;
      Serial.printf("[ImageSaver] Removed %s\n", name.c_str());
    } else {
      Serial.printf("[ImageSaver] Failed to remove %s\n", name.c_str());
    }
    file = root.openNextFile();
  }
  root.close();
  Serial.printf("[ImageSaver] Cleared %d files from /photos\n", removed);
}
