#pragma once
#include <Arduino.h>
#define FILE_WRITE "w"

extern std::map<std::string,std::string> g_fs;
extern bool g_fsMountOk;
extern bool   g_fsWritable;   // false = minden megnyitás írásra elbukik
extern size_t g_fsCapacity;   // 0 = korlátlan; egyébként a tárolható összméret
extern bool   g_fsRemoveOk;   // a remove() sikerül-e
extern size_t g_fsUsed();

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
  // Szimulált írás: a kapacitás túllépésekor rövidebb visszatérési érték,
  // mint a valóságban egy megtelt fájlrendszernél.
  size_t print(const char* s) {
    if (!ok_) return 0;
    std::string want(s);
    if (g_fsCapacity) {
      size_t other = 0;
      for (auto& kv : g_fs) if (kv.first != path_) other += kv.second.size();
      if (other + want.size() > g_fsCapacity) {
        size_t room = g_fsCapacity > other ? g_fsCapacity - other : 0;
        want = want.substr(0, room);
        g_fs[path_] = want;
        return want.size();
      }
    }
    g_fs[path_] = want;
    return want.size();
  }
  void flush() {}
private:
  std::string path_, data_; size_t pos_ = 0; bool ok_;
};

namespace fs {
class FS {
public:
  File open(const char* p) { return File(p, g_fs.count(p) > 0); }
  File open(const char* p, const char* mode) {
    (void)mode;
    if (!g_fsWritable) return File();   // nem nyitható írásra
    g_fs[p] = "";                        // FILE_WRITE csonkol
    return File(p, true);
  }
  bool remove(const char* p) { if (!g_fsRemoveOk) return false; return g_fs.erase(p) > 0; }
  bool remove(const String& p) { return remove(p.c_str()); }
  size_t totalBytes() { return g_fsCapacity ? g_fsCapacity : 1048576; }
  size_t usedBytes() { return g_fsUsed(); }
  bool begin(bool) { return g_fsMountOk; }
  bool exists(const char* p) { return g_fs.count(p) > 0; }
  bool exists(const String& p) { return g_fs.count(p.c_str()) > 0; }
};
}

class LittleFSClass : public fs::FS {};
extern LittleFSClass LittleFS;
