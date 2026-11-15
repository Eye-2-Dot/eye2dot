// Wi-Fi·서버 주소 견본. 이 파일을 같은 폴더에 secrets.h 로 복사한 뒤 값을 채웁니다.
// 펌웨어 1 의 firmware1/cam/include/secrets.h 와 같은 형식이라, 그 파일을 그대로 복사해도 됩니다.
// secrets.h 는 git 에 올리지 않습니다.
#pragma once

#define WIFI_SSID_VALUE     "your-hotspot-name"       // 2.4GHz 만 가능 (ESP32 는 5GHz 를 못 씀)
#define WIFI_PASSWORD_VALUE "your-hotspot-password"
#define SERVER_IP_VALUE     "192.168.137.1"           // 노트북 IP (윈도우 핫스팟 기본값. 다르면 ipconfig 의 IPv4 주소)
