// ============================================================================
// FUZZING A BEMENET-ELEMZO FELULETEN (clang libFuzzer)
//
// MIERT? A tobbi teszt - a 295 kezzel irt forgatokonyv es a 8 veletlen
// allapotgep-bejaras is - ERVENYES vagy legalabbis ELKEPZELT bemenetekkel
// dolgozik. A fuzzer nem: az kifejezetten olyan bajtsorozatokat keres, amikre
// senki nem gondolt, es a lefedettseg alapjan tanul, merre erdemes menni.
//
// A CELPONTOK. Ami "bemenetet elemez", vagyis ahol kulso adatbol lesz belso
// allapot:
//
//   1. A POST URLAP-ELEMZO. EZ A LENYEG. Ez az EGYETLEN felulet, aminek a
//      tuloldalan tenyleges tamado ulhet: AP modban barki csatlakozhat a
//      nyitott halozatra es POST-olhat, amit akar. A tobbi harom celpont
//      olyan adatot olvas, amit maga az eszkoz irt ki.
//   2. A jelszo-dekodolo (decodeSecretInPlace) - serult vagy idegen kezzel
//      irt /pass.txt.
//   3. Az /evlog.bin betolto - serult naplofajl (felbeszakadt iras, megtelt
//      fajlrendszer, flash-hiba).
//   4. A konfig-ertek olvaso (readConfigValue) - tetszoleges fajltartalom.
//
// A MERES HATARA, KIMONDVA. Ez HOST-on fut, stub Arduino API-k felett. Amit
// megfog: puffertulcsordulas, olvasas a hatarokon tul, definialatlan
// viselkedes, vegtelen ciklus, assert - a SAJAT kodunkban. Amit NEM fog meg:
// a valodi ESPAsyncWebServer / lwIP hibait (azok nem a mi kodunk es nem is
// forognak itt), es az IPAddress::fromString-et sem, mert az a hoston STUB -
// azt fuzzolni a sajat stubunk tesztelese lenne, nem a firmware-e.
//
// HASZNALAT:
//     make fuzz              minden celpont 30 mp
//     FUZZSEC=300 make fuzz  hosszabban
//     build/fuzz-post -runs=100000
// Talalat eseten a libFuzzer kiirja a bemenetet es elmenti (crash-*.
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <esp_system.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

#include "../limits_config.h"
#include "../configstore.h"
#include "../secret.h"
#include "../eventlog.h"
#include "../webportal.h"
#include "../sync.h"

extern char ssid[]; extern char pass[]; extern char ipStr[]; extern char gatewayStr[];
enum DeviceMode : uint8_t;
extern DeviceMode deviceMode;
extern std::map<std::string, ArRequestHandlerFunction> g_handlers;

// A kornyezet visszaallitasa MINDEN iteracio elott. Enelkul egy korabbi
// bemenet allapota atszivarogna a kovetkezobe, es a talalat NEM LENNE
// REPRODUKALHATO - egy fuzzer-talalat pedig pontosan attol ertekes, hogy a
// mentett bemenet ujra elojatszhato.
static void fuzzReset() {
  g_fs.clear();
  g_fsMountOk = true; g_fsWritable = true; g_fsRemoveOk = true;
  g_fsReadable = true; g_fsCapacity = 0; g_fsShortReadSkip = 0;
  g_millis = 1;
  g_log.clear(); g_serialLog.clear();
  ssid[0] = pass[0] = ipStr[0] = gatewayStr[0] = '\0';
  rtcEvMagic = 0; rtcEvNext = 0;
  clearRestartRequest(); endConfigWrite();
  setFilesystemReady(true);
}

// ---------------------------------------------------------------------------
#if FUZZ_TARGET == 1
// A POST URLAP-ELEMZO. A bemenetet mezokre vagjuk: [nev-index][hossz][ertek].
// A nev-index a NEGY ISMERT mezonevbol valaszt, plusz egy teljesen szabad
// nevet - igy a fuzzer egyszerre tudja terhelni az ERTEKEKET (ez a fontos) es
// probalkozni ismeretlen mezokkel is.
static const char* kNames[] = { "ssid", "pass", "ip", "gateway" };

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static bool inited = false;
  if (!inited) { startWebPortal(); inited = true; }
  fuzzReset();

  AsyncWebServerRequest req;
  size_t i = 0;
  std::string szabadNev;
  while (i + 2 <= size && req.params() < 24) {
    const uint8_t nevIdx = data[i++];
    size_t len = data[i++];
    if (len > size - i) len = size - i;
    std::string ertek((const char*)data + i, len);
    i += len;
    // A String NUL-terminalt: a beagyazott nullak utani resz ugyis elveszne,
    // de a hosszellenorzesek szempontjabol epp ez az erdekes eset.
    const bool post = (nevIdx & 0x80) == 0;
    if ((nevIdx & 0x7F) < 4) {
      req.addParam(kNames[nevIdx & 0x7F], ertek.c_str(), post);
    } else {
      szabadNev = "f" + std::to_string(nevIdx);
      req.addParam(szabadNev.c_str(), ertek.c_str(), post);
    }
  }
  try {
    g_handlers["/#2"](&req);
  } catch (RestartSignal&) {
  } catch (DeepSleepSignal&) {
  }
  return 0;
}

// ---------------------------------------------------------------------------
#elif FUZZ_TARGET == 2
// A JELSZO-DEKODOLO. A /pass.txt tartalma serult lehet (megszakadt iras,
// flash-hiba), vagy szandekosan atirt, ha valaki hozzafert a flashhez.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fuzzReset();
  // A valodi hivo egy PASS_MAX_LEN meretu pufferbe olvas be a fajlbol, es AZT
  // dekodolja helyben. A puffermeretet ezert itt is pontosan ugy valasztjuk.
  char buf[PASS_MAX_LEN + 1];
  size_t n = size < sizeof(buf) - 1 ? size : sizeof(buf) - 1;
  memcpy(buf, data, n);
  buf[n] = '\0';
  decodeSecretInPlace(buf);
  // Es a masik irany: barmilyen nyilt szoveg kodolasa se csorduljon tul.
  char out[PASS_MAX_LEN * 2 + 8];
  encodeSecret(buf, out, sizeof(out));
  return 0;
}

// ---------------------------------------------------------------------------
#elif FUZZ_TARGET == 3
// AZ /evlog.bin BETOLTO. Serult naplofajl: felbeszakadt iras, megtelt
// fajlrendszer, flash-hiba. A fejlecben allo "count" FAJLBOL szarmazo ertek.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fuzzReset();
  g_fs["/evlog.bin"] = std::string((const char*)data, size);
  EvFileHeader fej;
  memset(&fej, 0, sizeof(fej));
  if (loadEventLogHeader(fej)) {
    // A hivo szerzodese szerint EVLOG_SIZE elemu puffer kell. Ha a betolto a
    // fajlbol vett count-ot hataellenorzes nelkul hasznalna, ez itt tulirna -
    // es az ASan azonnal jelezne. (A kod ma vedekezik ellene; ez a teszt azt
    // rogziti, hogy ez igy is maradjon.)
    EventEntry puffer[EVLOG_SIZE];
    memset(puffer, 0, sizeof(puffer));
    loadEventLogEntries(fej, puffer);
  }
  // A hataron atnyulo hivas kozvetlenul is: a modul a HIVOTOL FUGGETLENUL is
  // biztonsagos kell legyen (lasd a sajat hatarellenorzeset).
  EvFileHeader hamis;
  memset(&hamis, 0, sizeof(hamis));
  if (size >= 2) { hamis.count = (uint8_t)data[0]; hamis.version = data[1]; }
  EventEntry puffer2[EVLOG_SIZE];
  memset(puffer2, 0, sizeof(puffer2));
  loadEventLogEntries(hamis, puffer2);
  return 0;
}

// ---------------------------------------------------------------------------
#elif FUZZ_TARGET == 4
// A KONFIG-ERTEK OLVASO. Tetszoleges fajltartalom -> fix meretu puffer.
// Mind a negy mezo a sajat, KULONBOZO meretu pufferevel - epp a legkisebbnel
// (IPSTR_MAX_LEN) a legszukebb a hely.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  fuzzReset();
  const std::string tartalom((const char*)data, size);
  g_fs["/ssid.txt"] = tartalom;
  g_fs["/pass.txt"] = tartalom;
  g_fs["/ip.txt"] = tartalom;
  g_fs["/gateway.txt"] = tartalom;

  char s[SSID_MAX_LEN + 1];
  char p[PASS_MAX_LEN + 1];
  char ip[IPSTR_MAX_LEN + 1];
  char gw[IPSTR_MAX_LEN + 1];
  readConfigValue(LittleFS, "/ssid.txt", s, sizeof(s));
  readConfigValue(LittleFS, "/pass.txt", p, sizeof(p));
  readConfigValue(LittleFS, "/ip.txt", ip, sizeof(ip));
  readConfigValue(LittleFS, "/gateway.txt", gw, sizeof(gw));
  // A fajl tartalmanak OSSZEHASONLITASA is a mi kodunk (fileMatches):
  // ez dont arrol, kell-e ujrairni a flasht.
  fileMatches(LittleFS, "/ssid.txt", s, strlen(s));
  return 0;
}
#else
#error "FUZZ_TARGET 1..4 kozul valassz"
#endif
