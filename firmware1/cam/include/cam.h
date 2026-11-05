/*
 * Eye 2 Dot — ESP32-CAM
 * cam.h — 카메라 초기화·촬영, 보드 내장 LED(GPIO4)
 */

#pragma once

#include "esp_camera.h"

const int PIN_FLASH = 4;   // 보드 내장 LED (촬영할 때 켬)

// 카메라를 켠다. 실패하면 false (리본 케이블·전원 확인)
bool camInit();

// LED 를 켜고 한 장 찍는다. 실패하면 nullptr. 다 쓴 뒤 camRelease() 로 반납할 것
camera_fb_t* camCapture();

void camRelease(camera_fb_t* fb);
