#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <functional>

#define HTTP_GET 1
#define HTTP_POST 2

class AsyncWebParameter {
public:
  const String& name() const { return n_; }
  const String& value() const { return v_; }
  bool isPost() const { return true; }
private:
  String n_, v_;
};

class AsyncWebServerRequest {
public:
  size_t params() const { return 0; }
  const AsyncWebParameter* getParam(size_t) const { return nullptr; }
  void send(int, const char*, const String&) {}
  void send(fs::FS&, const char*, const char*) {}
};

typedef std::function<void(AsyncWebServerRequest*)> ArRequestHandlerFunction;

class AsyncWebServer {
public:
  AsyncWebServer(int) {}
  void on(const char*, int, ArRequestHandlerFunction) {}
  void onNotFound(ArRequestHandlerFunction) {}
  void begin() {}
  void end() {}
};
