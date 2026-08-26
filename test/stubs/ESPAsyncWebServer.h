#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <functional>
#include <cstdarg>

#define HTTP_GET 1
#define HTTP_POST 2

class AsyncWebParameter {
public:
  AsyncWebParameter(const char* n, const char* v, bool post = true)
    : n_(n), v_(v), post_(post) {}
  const String& name() const { return n_; }
  const String& value() const { return v_; }
  bool isPost() const { return post_; }
private:
  String n_, v_; bool post_;
};

// Stream valasz: a tesztek a _body-ban latjak, amit a handler kiirt
class AsyncResponseStream : public Print {
public:
  std::string out;
  size_t print(const char* s) { out += s; return 0; }
  size_t print(const String& s) { out += s.c_str(); return 0; }
  size_t printf(const char* f, ...) {
    char buf[512]; va_list ap; va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap); va_end(ap);
    out += buf; return 0;
  }
};

// A tesztek ezen keresztül hajtják meg a regisztrált handlereket.
class AsyncWebServerRequest {
public:
  std::vector<AsyncWebParameter> _params;
  int _code = 0;
  std::string _body;

  void addParam(const char* n, const char* v, bool post = true) {
    _params.push_back(AsyncWebParameter(n, v, post));
  }
  size_t params() const { return _params.size(); }
  const AsyncWebParameter* getParam(size_t i) const { return &_params[i]; }
  const AsyncWebParameter* getParam(int i) const { return &_params[(size_t)i]; }
  void send(int c, const char*, const String& b) { _code = c; _body = b.c_str(); }
  void send(int c, const char*, const char* b) { _code = c; _body = b ? b : ""; }
  void send(fs::FS&, const char*, const char*) { _code = 200; _body = "<file>"; }
  AsyncResponseStream* beginResponseStream(const char*, size_t = 1460) {
    _stream = new AsyncResponseStream(); return _stream;
  }
  void send(AsyncResponseStream* r) { _code = 200; _body = r->out; delete r; _stream = nullptr; }
  AsyncResponseStream* _stream = nullptr;
};

typedef std::function<void(AsyncWebServerRequest*)> ArRequestHandlerFunction;

extern std::map<std::string, ArRequestHandlerFunction> g_handlers;

class AsyncWebServer {
public:
  AsyncWebServer(int) {}
  void on(const char* path, int method, ArRequestHandlerFunction fn) {
    g_handlers[std::string(path) + "#" + std::to_string(method)] = fn;
  }
  void onNotFound(ArRequestHandlerFunction fn) { g_handlers["404"] = fn; }
  void begin() {}
  void end() {}
};
