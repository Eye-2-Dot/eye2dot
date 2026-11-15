#pragma once
#include <Arduino.h>
struct HTTPClient {
  String url, resp;
  void setConnectTimeout(int) {} void setTimeout(int) {}
  bool begin(const String& u) { url = u; return true; }
  void addHeader(const char*, const char*) {}
  int GET(); int POST(const String& body);
  String getString() { return resp; }
  String errorToString(int) { return String("connection refused"); }
  void end() {}
};
