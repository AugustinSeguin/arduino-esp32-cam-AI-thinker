#include "app_sd_manager.h"
#include "app_api_client.h"
#include "esp_camera.h"
#include <SD_MMC.h>
#include <FS.h>
#include <Arduino.h>
#include <time.h>
#include "ff.h"
#include <string.h>
#include "esp32-hal-ledc.h"

// server upload variables defined in main sketch
extern const char *CAMERA_KEY;

static TaskHandle_t s_saveTaskHandle = NULL;
static volatile bool s_taskRunning = false;

// Try to initialize SD_MMC and return whether it succeeded
bool sdInit() {
  if (SD_MMC.begin()) {
    Serial.println("[SDMgr] SD_MMC mounted successfully");
    return true;
  } else {
    Serial.println("[SDMgr] SD_MMC mount failed");
    return false;
  }
}

// Utility: create /photos folder if missing
static void ensurePhotosDir() {
  if (!SD_MMC.exists("/photos")) {
    SD_MMC.mkdir("/photos");
  }
}

// Compute total bytes used by files in /photos
static uint64_t getPhotosFolderSize() {
  if (!SD_MMC.exists("/photos")) return 0;
  File root = SD_MMC.open("/photos");
  if (!root) return 0;
  uint64_t total = 0;
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      total += (uint64_t)file.size();
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  return total;
}

// Remove the oldest file (lexicographically earliest name) from /photos
static bool removeOldestPhoto() {
  if (!SD_MMC.exists("/photos")) return false;
  File root = SD_MMC.open("/photos");
  if (!root) return false;
  String oldestName;
  uint64_t oldestSize = 0;
  bool found = false;
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String name = file.name();
      if (!found || name < oldestName) {
        oldestName = name;
        oldestSize = (uint64_t)file.size();
        found = true;
      }
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  if (!found) return false;
  if (SD_MMC.remove(oldestName)) {
    Serial.printf("[SDMgr] Removed %s (%llu bytes) to free space\n", oldestName.c_str(), (unsigned long long)oldestSize);
    return true;
  } else {
    Serial.printf("[SDMgr] Failed to remove %s\n", oldestName.c_str());
    return false;
  }
}

// Ensure /photos usage is under limitBytes by removing oldest files
static void enforceStorageLimit(uint64_t limitBytes) {
  uint64_t used = getPhotosFolderSize();
  Serial.printf("[SDMgr] Storage used: %llu bytes (limit %llu)\n", (unsigned long long)used, (unsigned long long)limitBytes);
  while (used > limitBytes) {
    if (!removeOldestPhoto()) break;
    used = getPhotosFolderSize();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  Serial.printf("[SDMgr] Storage check complete: %llu bytes used\n", (unsigned long long)used);
}

// Motion detection task: capture, analyze, save images
static void imageSaveTask(void* pv) {
  uint32_t intervalSeconds = 5;
  if (pv) {
    uint32_t *p = (uint32_t*)pv;
    intervalSeconds = *p;
    free(p);
  }
  if (intervalSeconds == 0) intervalSeconds = 5;
  s_taskRunning = true;

  // Motion detection parameters
  const framesize_t MOTION_FRAMESIZE = FRAMESIZE_QQVGA; // 160x120
  const uint8_t MOTION_PIXEL_DIFF_THRESHOLD = 5;
  const uint8_t MOTION_SENSITIVITY_PERCENT = 1;

  sensor_t *sensor = esp_camera_sensor_get();
  const size_t motion_w = 160;
  const size_t motion_h = 120;
  const size_t motion_buf_len = motion_w * motion_h;
  uint8_t *prevBuf = (uint8_t*)malloc(motion_buf_len);
  if (prevBuf) memset(prevBuf, 0, motion_buf_len);
  bool havePrev = false;
  unsigned long lastPeriodicSave = 0;
  unsigned long lastMotionNotify = 0;
  const unsigned long MOTION_NOTIFY_COOLDOWN_MS = 10000UL; // 10s cooldown between notifications
  int skipFrameCount = 0;
  const int SKIP_INITIAL_FRAMES = 2; // Skip first 2 frames to establish baseline
  const uint8_t BRIGHTNESS_THRESHOLD = 80; // Luminosity threshold (below = night)
  uint8_t *lastJpegData = NULL;
  size_t lastJpegLen = 0;

  while (s_taskRunning) {
    if (!sdInit()) {
      Serial.println("[SDMgr] SD not available, retrying later...");
      vTaskDelay(pdMS_TO_TICKS(intervalSeconds * 1000UL));
      continue;
    }

    ensurePhotosDir();

    // Enforce storage threshold: keep usage under 6 GiB
    const uint64_t STORAGE_LIMIT = (uint64_t)6ULL * 1024ULL * 1024ULL * 1024ULL;
    enforceStorageLimit(STORAGE_LIMIT);

    // Motion detection: compare JPEG data size and content
    bool motionDetected = false;
    bool isNight = false;
    if (sensor) {
      // Capture QVGA JPEG for motion detection
      sensor->set_pixformat(sensor, PIXFORMAT_JPEG);
      sensor->set_framesize(sensor, FRAMESIZE_QVGA);
      camera_fb_t *fb = esp_camera_fb_get();
      
      if (fb && fb->format == PIXFORMAT_JPEG) {
        // Estimate brightness from JPEG header
        uint8_t brightness = 100; // default
        if (fb->len > 100) {
          uint32_t sum = 0;
          for (size_t i = 20; i < 100 && i < fb->len; i++) {
            sum += fb->buf[i];
          }
          brightness = (uint8_t)(sum / 80);
        }
        isNight = (brightness < BRIGHTNESS_THRESHOLD);
        
        Serial.printf("[SDMgr] JPEG frame: %u bytes [brightness:%u %s]\n", 
                      (unsigned)fb->len, brightness, isNight ? "(NUIT)" : "(JOUR)");
        
        // Skip initial frames to establish baseline
        if (skipFrameCount < SKIP_INITIAL_FRAMES) {
          skipFrameCount++;
          lastJpegLen = fb->len;
          Serial.printf("[SDMgr] Skipping frame %d to establish baseline\n", skipFrameCount);
        } else if (lastJpegLen > 0) {
          // Compare JPEG sizes and first bytes
          int sizeDiff = (int)fb->len - (int)lastJpegLen;
          if (sizeDiff < 0) sizeDiff = -sizeDiff;
          
          // Check byte difference in first part of frame
          size_t checkSize = (fb->len > 200 && lastJpegData) ? 200 : 0;
          size_t bytesDiff = 0;
          if (checkSize > 0 && lastJpegData) {
            for (size_t i = 0; i < checkSize; i++) {
              if (fb->buf[i] != lastJpegData[i]) bytesDiff++;
            }
          }
          
          float diffPercent = (checkSize > 0) ? (100.0f * bytesDiff / checkSize) : 0;
          int sizeDiffPercent = (lastJpegLen > 0) ? (100 * sizeDiff / lastJpegLen) : 0;
          
          Serial.printf("[SDMgr] Motion: size_diff=%d%% (%d bytes), content_diff=%.1f%% (%u/%u bytes)\n",
                        sizeDiffPercent, sizeDiff, diffPercent, (unsigned)bytesDiff, (unsigned)checkSize);
          
          // Trigger motion if size changed >10% OR content changed >15%
          if (sizeDiffPercent > 10 || diffPercent > 15.0f) {
            motionDetected = true;
            Serial.printf("[SDMgr] *** MOTION DETECTED *** size_diff=%d%% content_diff=%.1f%%\n", 
                          sizeDiffPercent, diffPercent);
            
            unsigned long nowMs = millis();
            if ((nowMs - lastMotionNotify) >= MOTION_NOTIFY_COOLDOWN_MS) {
              lastMotionNotify = nowMs;
              notifyMotionToServer(CAMERA_KEY);
            }
          }
        } else {
          Serial.println("[SDMgr] Establishing first baseline JPEG");
        }
        
        // Save current JPEG data for next comparison
        if (lastJpegData) free(lastJpegData);
        lastJpegData = (uint8_t*)malloc(fb->len);
        if (lastJpegData) {
          memcpy(lastJpegData, fb->buf, fb->len);
          lastJpegLen = fb->len;
        }
      } else {
        if (fb) Serial.printf("[SDMgr] WARNING: Frame not JPEG format: %d\n", fb->format);
        else Serial.println("[SDMgr] ERROR: Failed to capture JPEG frame!");
      }
      if (fb) esp_camera_fb_return(fb);
    }

    // Decide whether to save: on motion or periodic interval
    unsigned long nowMillis = millis();
    bool doSave = false;
    if (motionDetected) doSave = true;
    else if ((nowMillis - lastPeriodicSave) >= (intervalSeconds * 1000UL)) doSave = true;

    if (!doSave) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    // Ensure flash LED is always off
#if defined(LED_GPIO_NUM)
    ledcWrite(LED_GPIO_NUM, 0);
#endif

    // Capture final JPEG at QVGA for saving
    if (sensor) {
      sensor->set_pixformat(sensor, PIXFORMAT_JPEG);
      sensor->set_framesize(sensor, FRAMESIZE_QVGA);
    }

    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[SDMgr] Camera capture failed");
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    if (fb->format != PIXFORMAT_JPEG) {
      Serial.println("[SDMgr] Frame not in JPEG format — skipping save");
      esp_camera_fb_return(fb);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    // Build filename using timestamp (YYYY-MM-DD_HH-MM-SS)
    char name[64];
    time_t now = time(nullptr);
    if (now > 1000000000UL) {
      struct tm tm;
      localtime_r(&now, &tm);
      strftime(name, sizeof(name), "%Y-%m-%d_%H-%M-%S", &tm);
    } else {
      uint32_t t = (uint32_t)(millis() / 1000UL);
      snprintf(name, sizeof(name), "up%lu", (unsigned long)t);
    }

    char path[128];
    snprintf(path, sizeof(path), "/photos/%s.jpg", name);
    int suffix = 0;
    while (SD_MMC.exists(path)) {
      ++suffix;
      snprintf(path, sizeof(path), "/photos/%s_%d.jpg", name, suffix);
    }

    // Write file to SD card
    File file = SD_MMC.open(path, FILE_WRITE);
    if (!file) {
      Serial.printf("[SDMgr] Failed to open file for writing: %s\n", path);
      esp_camera_fb_return(fb);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    size_t written = file.write(fb->buf, fb->len);
    file.close();

    // Set FAT timestamp on the file if we have valid NTP time
    now = time(nullptr);
    if (now > 1000000000UL) {
      struct tm tm;
      localtime_r(&now, &tm);
      WORD fdate = (WORD)(((tm.tm_year + 1900 - 1980) << 9) | ((tm.tm_mon + 1) << 5) | (tm.tm_mday));
      WORD ftime = (WORD)((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
      FILINFO finfo;
      memset(&finfo, 0, sizeof(finfo));
      finfo.fdate = fdate;
      finfo.ftime = ftime;
      FRESULT fr = f_utime((TCHAR*)path, &finfo);
      if (fr == FR_OK) {
        Serial.printf("[SDMgr] Set FAT timestamp for %s (%04d-%02d-%02d %02d:%02d:%02d)\n", path,
                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
      } else {
        Serial.printf("[SDMgr] f_utime failed (%d) for %s\n", fr, path);
      }
    }

    if (written == fb->len) {
      Serial.printf("[SDMgr] Saved %u bytes to %s\n", (unsigned)written, path);
    } else {
      Serial.printf("[SDMgr] Incomplete write %u/%u bytes to %s\n", (unsigned)written, (unsigned)fb->len, path);
    }

    esp_camera_fb_return(fb);
    lastPeriodicSave = millis();
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  if (prevBuf) free(prevBuf);
  if (lastJpegData) free(lastJpegData);
  s_saveTaskHandle = NULL;
  vTaskDelete(NULL);
}

void startImageSaverTask(uint32_t intervalSeconds) {
  if (s_saveTaskHandle != NULL) {
    Serial.println("[SDMgr] Task already running");
    return;
  }
  uint32_t *p = (uint32_t*)malloc(sizeof(uint32_t));
  if (!p) return;
  *p = intervalSeconds;

  xTaskCreatePinnedToCore(imageSaveTask, "image_saver", 8192, p, 1, &s_saveTaskHandle, 1);
  Serial.printf("[SDMgr] Started task with interval %us\n", (unsigned)intervalSeconds);
}

void stopImageSaverTask() {
  if (!s_saveTaskHandle) {
    Serial.println("[SDMgr] Task not running");
    return;
  }
  s_taskRunning = false;
  uint32_t t = 0;
  while (s_saveTaskHandle && (t++ < 20)) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  if (s_saveTaskHandle) {
    vTaskDelete(s_saveTaskHandle);
    s_saveTaskHandle = NULL;
  }
  Serial.println("[SDMgr] Stopped task");
}

void clearSavedImages() {
  if (!sdInit()) {
    Serial.println("[SDMgr] SD not available — cannot clear images");
    return;
  }
  if (!SD_MMC.exists("/photos")) {
    Serial.println("[SDMgr] /photos does not exist — nothing to clear");
    return;
  }

  File root = SD_MMC.open("/photos");
  if (!root) {
    Serial.println("[SDMgr] Failed to open /photos");
    return;
  }

  File file = root.openNextFile();
  int removed = 0;
  while (file) {
    String name = file.name();
    file.close();
    if (SD_MMC.remove(name)) {
      removed++;
      Serial.printf("[SDMgr] Removed %s\n", name.c_str());
    } else {
      Serial.printf("[SDMgr] Failed to remove %s\n", name.c_str());
    }
    file = root.openNextFile();
  }
  root.close();
  Serial.printf("[SDMgr] Cleared %d images\n", removed);
}
