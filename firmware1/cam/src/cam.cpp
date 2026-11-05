/*
 * Eye 2 Dot — ESP32-CAM
 * cam.cpp — 카메라 초기화·촬영, 보드 내장 LED(GPIO4)
 */

#include <Arduino.h>

#include "cam.h"

// AI Thinker ESP32-CAM 카메라 핀 (고정값, 건드리지 말 것)
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22


bool camInit() {
  pinMode(PIN_FLASH, OUTPUT);
  digitalWrite(PIN_FLASH, LOW);

  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk     = XCLK_GPIO_NUM;
  c.pin_pclk     = PCLK_GPIO_NUM;
  c.pin_vsync    = VSYNC_GPIO_NUM;
  c.pin_href     = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn     = PWDN_GPIO_NUM;
  c.pin_reset    = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;    // 하드웨어가 바로 JPEG를 만든다 (PNG 아님)
  c.grab_mode    = CAMERA_GRAB_LATEST; // 오래된 프레임 대신 최신 프레임

  if (psramFound()) {
    c.frame_size   = FRAMESIZE_XGA;   // 1024×768 — 서버 전처리 크기와 같아 전송량 절약
    c.jpeg_quality = 12;              // 낮을수록 고화질 (10~15 권장)
    c.fb_count     = 2;
    c.fb_location  = CAMERA_FB_IN_PSRAM;
  } else {
    c.frame_size   = FRAMESIZE_VGA;   // PSRAM 없으면 해상도를 낮춘다
    c.jpeg_quality = 15;
    c.fb_count     = 1;
    c.fb_location  = CAMERA_FB_IN_DRAM;
    Serial.println("경고: PSRAM 없음. Tools → PSRAM 설정을 확인하세요.");
  }

  return esp_camera_init(&c) == ESP_OK;
}


camera_fb_t* camCapture() {
  digitalWrite(PIN_FLASH, HIGH);
  delay(200);                          // 노출이 LED 밝기에 맞춰질 시간

  camera_fb_t* fb = esp_camera_fb_get();  // 첫 장은 LED 켜기 전 노출일 수 있어 버림
  if (fb) esp_camera_fb_return(fb);
  fb = esp_camera_fb_get();

  digitalWrite(PIN_FLASH, LOW);
  return fb;
}


void camRelease(camera_fb_t* fb) {
  if (fb) esp_camera_fb_return(fb);
}
