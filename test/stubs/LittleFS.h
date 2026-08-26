#pragma once
#include <Arduino.h>
#define FILE_WRITE "w"

extern std::map<std::string,std::string> g_fs;
extern bool g_fsMountOk;

class File : public Stream {
public:
  File() : ok_(false) {}
  File(const std::string& path, bool exists) : path_(path), ok_(exists) {
    if (ok_) data_ = g_fs[path];
  }
  operator bool() const { return ok_; }
  bool isDirectory() { return false; }
  void close() {}
  size_t size() { return data_.size(); }
  size_t read(uint8_t* b, size_t n) {
    size_t c = n < data_.size() - pos_ ? n : data_.size() - pos_;
    memcpy(b, data_.data() + pos_, c); pos_ += c; return c;
  }
  size_t print(const char* s) { g_fs[path_] = s; return strlen(s) ? strlen(s) : 1; }
private:
  std::string path_, data_; size_t pos_ = 0; bool ok_;
};

namespace fs {
class FS {
public:
  File open(const char* p) { return File(p, g_fs.count(p) > 0); }
  File open(const char* p, const char* mode) {
    (void)mode; g_fs[p] = ""; return File(p, true);
  }
  bool begin(bool) { return g_fsMountOk; }
  bool exists(const char* p) { return g_fs.count(p) > 0; }
  bool exists(const String& p) { return g_fs.count(p.c_str()) > 0; }
};
}

class LittleFSClass : public fs::FS {};
extern LittleFSClass LittleFS;
