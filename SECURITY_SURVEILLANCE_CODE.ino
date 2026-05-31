/*
  Security Surveillance System
  ESP32-CAM + PIR Sensor + Blynk + Telegram

  This root-level sketch is a sanitized single-file copy for easy GitHub viewing.
  Replace the placeholder values below before uploading to the ESP32-CAM.

  The modular project version is available in:
  src/security_surveillance/
*/

#define BLYNK_PRINT Serial

#define BLYNK_TEMPLATE_ID "TMPLxxxxxxxx"
#define BLYNK_TEMPLATE_NAME "Security Cam"
#define BLYNK_AUTH_TOKEN "replace-with-blynk-device-auth-token"

#define WIFI_SSID "replace-with-wifi-name"
#define WIFI_PASSWORD "replace-with-wifi-password"

#define TELEGRAM_BOT_TOKEN "replace-with-telegram-bot-token"
#define TELEGRAM_CHAT_ID "replace-with-telegram-chat-id"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <BlynkSimpleEsp32.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include <string.h>

#define PIR_PIN 13
#define FLASH_LED_PIN 4

#define VPIN_STREAM_URL V0
#define VPIN_MANUAL_CAPTURE V1
#define VPIN_CAPTURE_URL V2

// AI Thinker ESP32-CAM pin map.
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

WiFiClientSecure telegramClient;
String localIpAddress;

static httpd_handle_t cameraHttpd = NULL;
static httpd_handle_t streamHttpd = NULL;

bool connectWiFi();
void startCameraServer();
void sendPhotoTelegram(camera_fb_t *fb);
void takePhoto();
bool detectValidMotion();
bool heuristicDetect();
void publishBlynkUrls();
void maintainBlynkConnection();

static esp_err_t indexHandler(httpd_req_t *req) {
  const char *html =
      "<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>ESP32-CAM Security Surveillance</title></head>"
      "<body><h1>ESP32-CAM Security Surveillance</h1>"
      "<p><a href=\"/capture\">Capture photo</a></p>"
      "<p>Stream endpoint: <code>http://device-ip:81/stream</code></p>"
      "</body></html>";

  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t captureHandler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");

  esp_err_t result = httpd_resp_send(req, reinterpret_cast<const char *>(fb->buf), fb->len);
  esp_camera_fb_return(fb);
  return result;
}

static esp_err_t streamHandler(httpd_req_t *req) {
  static const char *streamContentType = "multipart/x-mixed-replace;boundary=frame";
  static const char *streamBoundary = "\r\n--frame\r\n";
  static const char *streamPart = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

  char partBuffer[64];
  esp_err_t result = httpd_resp_set_type(req, streamContentType);
  if (result != ESP_OK) {
    return result;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera stream capture failed");
      result = ESP_FAIL;
      break;
    }

    size_t headerLength = snprintf(partBuffer, sizeof(partBuffer), streamPart, static_cast<unsigned int>(fb->len));

    result = httpd_resp_send_chunk(req, streamBoundary, strlen(streamBoundary));
    if (result == ESP_OK) {
      result = httpd_resp_send_chunk(req, partBuffer, headerLength);
    }
    if (result == ESP_OK) {
      result = httpd_resp_send_chunk(req, reinterpret_cast<const char *>(fb->buf), fb->len);
    }

    esp_camera_fb_return(fb);

    if (result != ESP_OK) {
      break;
    }
  }

  return result;
}

void startCameraServer() {
  httpd_config_t cameraConfig = HTTPD_DEFAULT_CONFIG();
  cameraConfig.server_port = 80;

  httpd_uri_t indexUri = {};
  indexUri.uri = "/";
  indexUri.method = HTTP_GET;
  indexUri.handler = indexHandler;

  httpd_uri_t captureUri = {};
  captureUri.uri = "/capture";
  captureUri.method = HTTP_GET;
  captureUri.handler = captureHandler;

  if (httpd_start(&cameraHttpd, &cameraConfig) == ESP_OK) {
    httpd_register_uri_handler(cameraHttpd, &indexUri);
    httpd_register_uri_handler(cameraHttpd, &captureUri);
  } else {
    Serial.println("Failed to start camera HTTP server");
  }

  httpd_config_t streamConfig = HTTPD_DEFAULT_CONFIG();
  streamConfig.server_port = 81;
  streamConfig.ctrl_port = 32769;

  httpd_uri_t streamUri = {};
  streamUri.uri = "/stream";
  streamUri.method = HTTP_GET;
  streamUri.handler = streamHandler;

  if (httpd_start(&streamHttpd, &streamConfig) == ESP_OK) {
    httpd_register_uri_handler(streamHttpd, &streamUri);
  } else {
    Serial.println("Failed to start stream HTTP server");
  }
}

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");
  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < 30000) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection failed");
    return false;
  }

  localIpAddress = WiFi.localIP().toString();
  Serial.println("WiFi connected");
  Serial.println("Capture: http://" + localIpAddress + "/capture");
  Serial.println("Stream: http://" + localIpAddress + ":81/stream");
  return true;
}

void sendPhotoTelegram(camera_fb_t *fb) {
  if (WiFi.status() != WL_CONNECTED || fb == NULL) {
    return;
  }

  telegramClient.setInsecure();
  telegramClient.setTimeout(10000);

  if (!telegramClient.connect("api.telegram.org", 443)) {
    Serial.println("Telegram connection failed");
    return;
  }

  String boundary = "----ESP32CamBoundary";
  String bodyStart = "--" + boundary + "\r\n";
  bodyStart += "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n";
  bodyStart += TELEGRAM_CHAT_ID;
  bodyStart += "\r\n--" + boundary + "\r\n";
  bodyStart += "Content-Disposition: form-data; name=\"photo\"; filename=\"esp32cam.jpg\"\r\n";
  bodyStart += "Content-Type: image/jpeg\r\n\r\n";

  String bodyEnd = "\r\n--" + boundary + "--\r\n";
  int contentLength = bodyStart.length() + fb->len + bodyEnd.length();

  telegramClient.printf("POST /bot%s/sendPhoto HTTP/1.1\r\n", TELEGRAM_BOT_TOKEN);
  telegramClient.println("Host: api.telegram.org");
  telegramClient.println("Content-Type: multipart/form-data; boundary=" + boundary);
  telegramClient.println("Content-Length: " + String(contentLength));
  telegramClient.println("Connection: close");
  telegramClient.println();
  telegramClient.print(bodyStart);
  telegramClient.write(fb->buf, fb->len);
  telegramClient.print(bodyEnd);

  unsigned long startMs = millis();
  while (telegramClient.connected() && millis() - startMs < 5000) {
    while (telegramClient.available()) {
      Serial.write(telegramClient.read());
    }
  }

  telegramClient.stop();
  Serial.println("Telegram photo request sent");
}

void publishBlynkUrls() {
  if (!Blynk.connected() || localIpAddress.length() == 0) {
    return;
  }

  String streamUrl = "http://" + localIpAddress + ":81/stream";
  Blynk.virtualWrite(VPIN_STREAM_URL, streamUrl);
  Blynk.setProperty(VPIN_STREAM_URL, "urls", streamUrl);
}

void takePhoto() {
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(150);

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    digitalWrite(FLASH_LED_PIN, LOW);
    return;
  }

  sendPhotoTelegram(fb);

  if (Blynk.connected()) {
    Blynk.logEvent("motion_detected", "Motion detected. Photo captured.");

    String captureUrl = "http://" + localIpAddress + "/capture?_cb=" + String(random(999999));
    Blynk.virtualWrite(VPIN_CAPTURE_URL, captureUrl);
    Blynk.setProperty(VPIN_CAPTURE_URL, "urls", captureUrl);
    Serial.println("Capture URL: " + captureUrl);
  }

  esp_camera_fb_return(fb);
  digitalWrite(FLASH_LED_PIN, LOW);
}

BLYNK_WRITE(V1) {
  if (param.asInt() == 1) {
    Serial.println("Manual capture requested from Blynk");
    takePhoto();
    Blynk.virtualWrite(VPIN_MANUAL_CAPTURE, 0);
  }
}

bool detectValidMotion() {
  return heuristicDetect();
}

bool heuristicDetect() {
  static int previousFrameLength = 0;

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    return false;
  }

  int frameLength = fb->len;
  bool moved = false;

  if (previousFrameLength != 0) {
    int diff = abs(frameLength - previousFrameLength);
    if (diff > 1500) {
      moved = true;
    }
  }

  previousFrameLength = frameLength;
  esp_camera_fb_return(fb);

  if (!moved) {
    return false;
  }

  camera_fb_t *sampleFrame = esp_camera_fb_get();
  if (!sampleFrame) {
    return false;
  }

  const uint8_t *buffer = sampleFrame->buf;
  size_t bufferSize = sampleFrame->len;
  size_t samples = bufferSize < 400 ? bufferSize : 400;
  if (samples == 0) {
    esp_camera_fb_return(sampleFrame);
    return false;
  }

  unsigned long sum = 0;
  unsigned long sumSquares = 0;

  for (size_t i = 0; i < samples; i++) {
    int value = buffer[i];
    sum += value;
    sumSquares += static_cast<unsigned long>(value) * value;
  }

  double mean = static_cast<double>(sum) / samples;
  double variance = static_cast<double>(sumSquares) / samples - mean * mean;

  esp_camera_fb_return(sampleFrame);

  if (variance > 200.0) {
    Serial.printf("Motion variance %.2f is valid\n", variance);
    return true;
  }

  Serial.printf("Motion variance %.2f ignored\n", variance);
  return false;
}

void maintainBlynkConnection() {
  static unsigned long lastAttemptMs = 0;

  if (Blynk.connected()) {
    Blynk.run();
    return;
  }

  if (millis() - lastAttemptMs > 10000) {
    lastAttemptMs = millis();
    Serial.println("Connecting to Blynk");
    Blynk.connect(1000);
    publishBlynkUrls();
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(PIR_PIN, INPUT);
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);
  randomSeed(micros());

  camera_config_t config = {};
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count = 2;

  if (psramFound()) {
    config.fb_location = CAMERA_FB_IN_PSRAM;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed");
    return;
  }

  if (!connectWiFi()) {
    return;
  }

  startCameraServer();
  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connect(10000);
  publishBlynkUrls();

  Serial.println("Setup complete");
}

void loop() {
  maintainBlynkConnection();

  static unsigned long lastMotionMs = 0;
  const unsigned long cooldownMs = 8000;

  if (digitalRead(PIR_PIN) == HIGH && millis() - lastMotionMs > cooldownMs) {
    lastMotionMs = millis();
    Serial.println("PIR triggered. Validating motion.");

    if (detectValidMotion()) {
      Serial.println("Valid motion detected. Capturing photo.");
      takePhoto();
    } else {
      Serial.println("Motion ignored after validation.");
    }
  }
}
