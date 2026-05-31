#include "esp_camera.h"
#include "esp_http_server.h"
#include <Arduino.h>
#include <string.h>

static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
static const char *STREAM_BOUNDARY = "\r\n--frame\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static httpd_handle_t camera_httpd = NULL;
static httpd_handle_t stream_httpd = NULL;

static esp_err_t index_handler(httpd_req_t *req) {
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

static esp_err_t capture_handler(httpd_req_t *req) {
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

static esp_err_t stream_handler(httpd_req_t *req) {
  char part_buf[64];
  esp_err_t result = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
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

    size_t header_len = snprintf(part_buf, sizeof(part_buf), STREAM_PART, static_cast<unsigned int>(fb->len));

    result = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (result == ESP_OK) {
      result = httpd_resp_send_chunk(req, part_buf, header_len);
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
  httpd_config_t camera_config = HTTPD_DEFAULT_CONFIG();
  camera_config.server_port = 80;

  httpd_uri_t index_uri = {};
  index_uri.uri = "/";
  index_uri.method = HTTP_GET;
  index_uri.handler = index_handler;
  index_uri.user_ctx = NULL;

  httpd_uri_t capture_uri = {};
  capture_uri.uri = "/capture";
  capture_uri.method = HTTP_GET;
  capture_uri.handler = capture_handler;
  capture_uri.user_ctx = NULL;

  if (httpd_start(&camera_httpd, &camera_config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
  } else {
    Serial.println("Failed to start camera HTTP server");
  }

  httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
  stream_config.server_port = 81;
  stream_config.ctrl_port = 32769;

  httpd_uri_t stream_uri = {};
  stream_uri.uri = "/stream";
  stream_uri.method = HTTP_GET;
  stream_uri.handler = stream_handler;
  stream_uri.user_ctx = NULL;

  if (httpd_start(&stream_httpd, &stream_config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  } else {
    Serial.println("Failed to start stream HTTP server");
  }
}
