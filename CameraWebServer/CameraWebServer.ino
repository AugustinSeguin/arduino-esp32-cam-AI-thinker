#include "esp_camera.h"
#include <WiFi.h>
#include <esp_err.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>
#include <HTTPClient.h>

// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"
// SD save and API client helpers
#include "app_sd_manager.h"
#include "app_api_client.h"
// AP mode helper for configuring WiFi credentials
#include "app_start_ap_mode.h"
// Auto-generated config from mycam-api/.env
#include "config.h"

// ===========================
// Configuration from config.h (generated from mycam-api/.env)
// Run: python3 generate_config.py to update
// ===========================
const char *default_ssid = WIFI_SSID;
const char *default_password = WIFI_PASSWORD;
int secondDelay = SAVE_INTERVAL;
const char *SERVER_API_URL = SERVER_API_URL_CONFIG;
const char *API_KEY = API_KEY_CONFIG;
const char *CAMERA_KEY = CAMERA_KEY_CONFIG;

void setupLedFlash();
void startCameraServer();

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  Serial.println("Initialisation ESP32-CAM (ESP_EYE / OV3660)...");
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init -- diagnostics + safe retries
  Serial.println("Camera init: printing pin mapping...");
  Serial.printf("Pins: XCLK=%d PCLK=%d VSYNC=%d HREF=%d\n", XCLK_GPIO_NUM, PCLK_GPIO_NUM, VSYNC_GPIO_NUM, HREF_GPIO_NUM);
  Serial.printf("Data: D0=%d D1=%d D2=%d D3=%d D4=%d D5=%d D6=%d D7=%d\n", Y2_GPIO_NUM, Y3_GPIO_NUM, Y4_GPIO_NUM, Y5_GPIO_NUM, Y6_GPIO_NUM, Y7_GPIO_NUM, Y8_GPIO_NUM, Y9_GPIO_NUM);
  Serial.printf("SCCB: SDA=%d SCL=%d PWDN=%d RESET=%d\n", SIOD_GPIO_NUM, SIOC_GPIO_NUM, PWDN_GPIO_NUM, RESET_GPIO_NUM);

#if defined(PWDN_GPIO_NUM)
  if (PWDN_GPIO_NUM != -1) {
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    digitalWrite(PWDN_GPIO_NUM, LOW); // ensure powered
    Serial.printf("Pulled PWDN (gpio %d) LOW\n", PWDN_GPIO_NUM);
    delay(10);
  }
#endif

#if defined(RESET_GPIO_NUM)
  if (RESET_GPIO_NUM != -1) {
    pinMode(RESET_GPIO_NUM, OUTPUT);
    // pulse reset
    digitalWrite(RESET_GPIO_NUM, LOW);
    delay(20);
    digitalWrite(RESET_GPIO_NUM, HIGH);
    Serial.printf("Pulsed RESET (gpio %d)\n", RESET_GPIO_NUM);
    delay(10);
  }
#endif

  esp_err_t err = ESP_FAIL;
  const uint32_t xclks[] = {20000000U, 10000000U, 8000000U};
  const int attempts = sizeof(xclks)/sizeof(xclks[0]);
  for (int a = 0; a < attempts; ++a) {
    config.xclk_freq_hz = xclks[a];
    Serial.printf("Attempt %d/%d: esp_camera_init with XCLK=%u\n", a + 1, attempts, (unsigned)config.xclk_freq_hz);
    err = esp_camera_init(&config);
    if (err == ESP_OK) {
      Serial.println("Camera initialized successfully");
      break;
    }
    Serial.printf("Camera init failed (attempt %d) 0x%x (%s)\n", a + 1, err, esp_err_to_name(err));
    // try pulsing reset/pwdn again between attempts
#if defined(RESET_GPIO_NUM)
    if (RESET_GPIO_NUM != -1) {
      Serial.println("Retry: pulsing RESET before next attempt");
      digitalWrite(RESET_GPIO_NUM, LOW);
      delay(10);
      digitalWrite(RESET_GPIO_NUM, HIGH);
      delay(20);
    }
#endif
#if defined(PWDN_GPIO_NUM)
    if (PWDN_GPIO_NUM != -1) {
      Serial.println("Retry: toggling PWDN before next attempt");
      digitalWrite(PWDN_GPIO_NUM, HIGH);
      delay(10);
      digitalWrite(PWDN_GPIO_NUM, LOW);
      delay(20);
    }
#endif
    delay(200);
  }
  if (err != ESP_OK) {
    Serial.printf("Camera init ultimately failed after %d attempts: 0x%x (%s)\n", attempts, err, esp_err_to_name(err));
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  // setupLedFlash();
#endif

  // Try to load saved credentials (from AP-mode configuration)
  String savedSsid, savedPass;
  if (loadSavedWiFi(savedSsid, savedPass)) {
    Serial.printf("Found saved WiFi credentials: SSID=%s\n", savedSsid.c_str());
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  } else {
    Serial.println("No saved WiFi credentials found — starting with defaults");
    WiFi.begin(default_ssid, default_password);
  }
  WiFi.setSleep(false);

  Serial.print("WiFi connecting");
  // wait up to 10 seconds for connection; otherwise start config AP
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 10000UL) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    // Envoie le heartbeat au backend dès la connexion WiFi
    updateCameraHeartbeatToServer();
    // Configure NTP to get real time for timestamped filenames
    Serial.println("Configuring NTP (pool.ntp.org, time.google.com)...");
    // Set timezone to Europe/Paris (CET/CEST)
    setenv("TZ", "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00", 1);
    tzset();

    // Initialize SNTP explicitly with multiple servers
    esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_setservername(2, "time.cloudflare.com");
    esp_sntp_init();

    // Wait up to 60s for time to be acquired
    unsigned long ntpStart = millis();
    while ((time(nullptr) < 1000000000UL) && (millis() - ntpStart) < 60000UL) {
      Serial.print(".");
      delay(500);
    }
    if (time(nullptr) >= 1000000000UL) {
      time_t now = time(nullptr);
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      char buf[64];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
      Serial.printf("\nNTP acquired: %s\n", buf);
    } else {
      Serial.println("\nNTP not acquired via SNTP, trying HTTP fallback...");
      // Try HTTP fallback to worldtimeapi.org to fetch unixtime
      HTTPClient http;
      http.setTimeout(5000);
      if (http.begin("http://worldtimeapi.org/api/ip")) {
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
          String payload = http.getString();
          int idx = payload.indexOf("\"unixtime\":");
          if (idx >= 0) {
            idx += 11; // move past "unixtime":
            unsigned long unixt = 0;
            while (idx < (int)payload.length() && isDigit(payload[idx])) {
              unixt = unixt * 10 + (payload[idx] - '0');
              idx++;
            }
            if (unixt > 1000000000UL) {
              struct timeval tv;
              tv.tv_sec = (time_t)unixt;
              tv.tv_usec = 0;
              settimeofday(&tv, NULL);
              time_t now = time(nullptr);
              struct tm timeinfo;
              localtime_r(&now, &timeinfo);
              char buf[64];
              strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
              Serial.printf("HTTP time acquired: %s\n", buf);
            } else {
              Serial.println("HTTP fallback returned invalid unixtime");
            }
          } else {
            Serial.println("HTTP fallback: unixtime not found in response");
          }
        } else {
          Serial.printf("HTTP fallback failed: code %d\n", code);
        }
        http.end();
      } else {
        Serial.println("HTTP fallback: failed to begin connection");
      }
      if (time(nullptr) < 1000000000UL) {
        Serial.println("No valid time acquired (SNTP+HTTP) — timestamps will use uptime");
      }
    }
  } else {
    Serial.println("");
    Serial.println("WiFi connect failed or timed out — starting configuration AP");
    startConfigAP();
  }

  // Start periodic image saver (every 2 seconds) ONLY if a valid NTP time was acquired.
  if (time(nullptr) >= 1000000000UL) {
    startImageSaverTask((uint32_t)secondDelay);
  } else {
    Serial.println("Image saver not started: no valid time available. Connect to the AP and configure WiFi to allow NTP.");
  }

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");

  // Démarre le serveur HTTP de la caméra
  startCameraServer();
}

void loop() {
  // Heartbeat périodique toutes les 60s
  static unsigned long lastHeartbeat = 0;
  unsigned long now = millis();
  if (now - lastHeartbeat > 60000UL) {
    updateCameraHeartbeatToServer();
    lastHeartbeat = now;
  }
  delay(1000);
}
