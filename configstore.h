// A MENTETT KONFIGURACIO: a negy ertek, a hozzajuk tartozo fajlok, es a
// LittleFS-en vegzett olvasas/iras.
//
// A modul CSAK tarol es ellenoriz. Nem dont arrol, mikor kell menteni (az a
// webportal, illetve a wifireset gomb dolga), es nem tudja, mi a "jo" ertek -
// csak azt, hogy egy iras visszaolvasva ugyanazt adja-e.
#pragma once

#include <Arduino.h>
#include <LittleFS.h>   // az fs::FS tipushoz

#include "limits_config.h"

// File paths to save input values permanently
extern char ssid[SSID_MAX_LEN + 1];
extern char pass[PASS_MAX_LEN + 1];
extern char ipStr[IPSTR_MAX_LEN + 1];
extern char gatewayStr[IPSTR_MAX_LEN + 1];

// A negy ertekhez tartozo fajl a LittleFS-en.
extern const char ssidPath[];
extern const char passPath[];
extern const char ipPath[];
extern const char gatewayPath[];

// A konfiguráció betöltésének háromféle kimenetele. A hiányzó fájl NEM hiba:
// ez az állapot az első indításnál és a wifireset gomb után is normális.
enum ConfigStatus : uint8_t {
  CONFIG_OK = 0,       // beolvasva (az érték lehet üres is)
  CONFIG_MISSING = 1,  // a fájl nem létezik -> nincs még konfiguráció
  CONFIG_ERROR = 2     // a fájl létezik, de nem olvasható -> végzetes hiba
};

// A fajlrendszer csatolasa (formatOnFail = true). Hamis, ha nem sikerult.
// A modul MEGJEGYZI az eredmenyt: onnantol a filesystemReady() adja vissza.
bool initLittleFS();

// Csatolva van-e a fajlrendszer? Ha nincs, a beallitasok nem menthetok, es a
// naplo mentese is kimarad (ez utobbi nem hiba - az RTC naplo attol meg el).
// A jelzo maga a modulon belul van: kivulrol csak ezen at kerdezheto.
bool filesystemReady();

// A fajlrendszer allapotanak kenyszeritett beallitasa.
//
// A PROGRAM MAGA NEM HIVJA - a jelzot kizarolag az initLittleFS() allitja be.
// Egyetlen hivoja a host tesztkeszlet, ami a "menet kozben kiesik a
// fajlrendszer" allapotot mashogy nem tudja eloallitani (a stub LittleFS-t a
// csatolas utan nem lehet "leszedni"). Ezert all itt kulon, nevesitve es
// megindokolva, ahelyett hogy a jelzot egyszeruen kivul hagynank: igy
// legalabb latszik, hogy EGY hivoja van, es az nem a program.
void setFilesystemReady(bool ready);

// Hasznalhato-e ez a cim statikus IPv4 konfiguracionak?
bool isUsableIPv4(const IPAddress& addr);

// Egy ertek beolvasasa. A vezeto/zaro whitespace-t levagja.
ConfigStatus readConfigValue(fs::FS& fs, const char* path, char* out, size_t outSize);

// Egy ertek kiirasa, VISSZAOLVASASOS ellenorzessel. Hamis, ha az iras nem
// sikerult, vagy a visszaolvasott tartalom nem egyezik.
bool writeConfigValue(fs::FS& fs, const char* path, const char* message);

// Egy ertek torlese (a fajl uresre irasa).
bool clearConfigValue(fs::FS& fs, const char* path);

// Egyezik-e a fajl tartalma a megadott ertekkel? (A visszaolvasasos
// ellenorzes epitokocka - a wifireset is hasznalja.)
bool fileMatches(fs::FS& fs, const char* path, const char* value, size_t len);
