#pragma once
#include <set>
#include <Arduino.h>
#define FILE_WRITE "w"

extern std::map<std::string,std::string> g_fs;
extern bool g_fsMountOk;
extern uint32_t g_fsMountMs;
extern bool   g_fsWritable;
extern size_t g_fsCapacity;
extern bool   g_fsRemoveOk;
extern bool   g_fsReadable;

extern bool   g_fsSilentWriteFail;

extern bool   g_fsShortRead;
extern int    g_fsShortReadSkip;
extern size_t g_fsUsed();

extern void (*g_onFsWrite)();

extern std::set<std::string> g_fsDirs;

class File : public Stream {
public:
  File() : ok_(false) {}
  File(const std::string& path, bool exists) : path_(path), ok_(exists) {
    if (ok_) data_ = g_fs[path];
  }
  operator bool() const { return ok_; }
  bool isDirectory() { return g_fsDirs.count(path_) != 0; }
  void close() {}
  size_t size() { return data_.size(); }
  size_t read(uint8_t* b, size_t n) {
    if (pos_ >= data_.size()) return 0;
    size_t c = n < data_.size() - pos_ ? n : data_.size() - pos_;

    if (g_fsShortRead) {
      if (g_fsShortReadSkip > 0) g_fsShortReadSkip--;
      else if (c > 0) c--;
    }
    memcpy(b, data_.data() + pos_, c); pos_ += c; return c;
  }

  size_t print(const char* s) {
    if (!ok_) return 0;
    if (g_onFsWrite) g_onFsWrite();

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
    if (g_fsSilentWriteFail) return want.size();
    g_fs[path_] = want;
    return want.size();
  }

  size_t write(const uint8_t* b, size_t n) {
    if (!ok_) return 0;
    if (g_onFsWrite) g_onFsWrite();

    std::string want(reinterpret_cast<const char*>(b), n);
    if (g_fsCapacity) {
      size_t other = 0;
      for (auto& kv : g_fs) if (kv.first != path_) other += kv.second.size();
      const size_t mar = g_fs[path_].size();
      if (other + mar + want.size() > g_fsCapacity) {
        size_t room = g_fsCapacity > other + mar ? g_fsCapacity - other - mar : 0;
        want = want.substr(0, room);
        g_fs[path_] += want;
        return want.size();
      }
    }
    if (g_fsSilentWriteFail) return want.size();
    g_fs[path_] += want;
    return n;
  }
  void flush() {}
private:
  std::string path_, data_; size_t pos_ = 0; bool ok_;
};

namespace fs {
class FS {
public:
  File open(const char* p) { return File(p, g_fsReadable && g_fs.count(p) > 0); }
  File open(const char* p, const char* mode) {
    if (mode && mode[0] == 'r') {
      return File(p, g_fsReadable && g_fs.count(p) > 0);
    }
    if (!g_fsWritable) return File();
    g_fs[p] = "";
    return File(p, true);
  }
  bool remove(const char* p) { if (!g_fsRemoveOk) return false; return g_fs.erase(p) > 0; }
  bool remove(const String& p) { return remove(p.c_str()); }
  size_t totalBytes() { return g_fsCapacity ? g_fsCapacity : 1048576; }
  size_t usedBytes() { return g_fsUsed(); }

  bool begin(bool) { g_millis += g_fsMountMs; wdtAdvanceTime(g_fsMountMs); return g_fsMountOk; }
  bool exists(const char* p) { return g_fs.count(p) > 0; }
  bool exists(const String& p) { return g_fs.count(p.c_str()) > 0; }
};
}

class LittleFSClass : public fs::FS {};
extern LittleFSClass LittleFS;
