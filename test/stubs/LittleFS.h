#pragma once
#include <Arduino.h>
#define FILE_WRITE "w"

extern std::map<std::string,std::string> g_fs;
extern bool g_fsMountOk;
extern uint32_t g_fsMountMs;   // a csatolas/formazas ideje (0 = azonnali)
extern bool   g_fsWritable;   // false = minden megnyitás írásra elbukik
extern size_t g_fsCapacity;   // 0 = korlátlan; egyébként a tárolható összméret
extern bool   g_fsRemoveOk;   // a remove() sikerül-e
extern bool   g_fsReadable;   // false = a létező fájl sem nyitható olvasásra
// A legalattomosabb hiba: az írás a helyes bájtszámot adja vissza, a tartalom
// mégsem kerül ki. Pontosan ezért nem elég a print() visszatérési értéke - a
// File::close() és a File::flush() ugyanis void, tehát a lezáráskori hibát
// másképp nem lehet észrevenni. Csak a visszaolvasás fogja meg.
extern bool   g_fsSilentWriteFail;
// A méret szerint kért olvasás kevesebb bájtot ad vissza (sérült fájlrendszer).
extern bool   g_fsShortRead;
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
    if (g_fsShortRead && c > 0) c--;      // sérült FS: rövidebb olvasás
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
    if (g_fsSilentWriteFail) return want.size();   // "sikeres" írás, üres fájl
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
  File open(const char* p) { return File(p, g_fsReadable && g_fs.count(p) > 0); }
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
  // A VALODI begin(true) elso indulaskor FORMAZ, es kozben senki nem eteti a
  // watchdogot. A firmware epp erre meretezte a WDT_TIMEOUT_MS-t (a 512 KiB-os
  // particio 128 szektora), tehat a harnessnek modelleznie kell az idot is -
  // kulonben a "watchdog a setup() elejen" dontes tesztelhetetlen maradna.
  bool begin(bool) { g_millis += g_fsMountMs; return g_fsMountOk; }
  bool exists(const char* p) { return g_fs.count(p) > 0; }
  bool exists(const String& p) { return g_fs.count(p.c_str()) > 0; }
};
}

class LittleFSClass : public fs::FS {};
extern LittleFSClass LittleFS;
