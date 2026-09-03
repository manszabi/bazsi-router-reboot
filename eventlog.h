// DIAGNOSZTIKAI ESEMENYNAPLO es a valos ido (NTP).
//
// Miert egy modul a ketto? Mert a naplo egyetlen kulso szolgaltatast hasznal,
// a valos idot - es forditva, az oraszinkronnak ebben a programban egyetlen
// celja van: hogy a naplo bejegyzesei ertelmezhetok legyenek. Kulon modulkent
// mindketto felig ureset mondana.
//
// A naplo RTC memoriaban elo korpuffer, amit a fajlrendszerre is kimentunk a
// fontos pillanatokban (router reset elott, AP modba valtas elott, minden
// enterDeepSleep() elott). Igy egy aramszunet sem viszi el.
#pragma once

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <stddef.h>
#include <stdint.h>

// RTC_NOINIT_ATTR: túléli a deep sleepet, a watchdog resetet ÉS a reset gombot
// is - vagyis pont azokat a hibákat, amiket ki akarunk vizsgálni. Csak az
// áramtalanítás törli. Az ESP32-C3-on ~8 KB RTC memória van; ez a napló
// 392 bájt (2 x 4 bájt fejléc + 32 x 12 bájt bejegyzés), a program összes RTC
// állapota együtt is ~420 bájt.
enum EventCode : uint8_t {
  EV_BOOT = 1,          // param: reset ok (esp_reset_reason_t)
  EV_WIFI_OK = 2,       // param: kör sorszám, amiben sikerült
  EV_WIFI_LOST = 3,     // param: WiFi.status()
  EV_TEST_FAIL = 4,     // param: teszt ciklus index, 1-alapú (1..5)
  EV_ROUTER_RESET = 5,  // param: hányadik reset esemény
  EV_AP_MODE = 6,       // param: ok (1=nincs SSID 2=auth hiba 3=2 nap letelt
                        //           4=statikus IP rossz: a gateway sem elerheto)
  EV_CONFIG_SAVED = 7,  // param: 0
  EV_SLEEP = 8,         // param: ok (1=retry 2=internet 3=AP timeout 4=fatal)
  EV_FATAL = 9,         // param: ok (1=FS mount 2=konfig olvasás 3=watchdog
                        //           4=wifireset törlés sikertelen
                        //           5=tartósan kevés a szabad heap)
  EV_WDT_RESET = 10,    // param: hányadik watchdog reset
  EV_STUCK_BUTTON = 11, // param: 0 = reset gomb, 1 = wifireset gomb
  EV_GW_UNREACHABLE = 12, // param: 1 = reset elott, 2 = a reset utan is
  EV_LOW_HEAP = 13,     // param: a szabad heap KiB-ban, a kuszob atlepesekor
  EV_HEAP_RESTART = 14  // param: a szabad heap KiB-ban az ujrainditas elott
};

// Pontosan 12 bájt. A kitöltő mező explicit, hogy a RTC memóriában tárolt
// elrendezés akkor se változzon, ha a fordító igazítási szabályai eltérnek -
// és ez most már nem csak az RTC-re igaz: ez a struktúra megy ki bájtról
// bájtra a LittleFS-re mentett naplófájlba is (lásd saveEventLog()), tehát a
// méret és a sorrend a FÁJLFORMÁTUM része. Ha valaha változik, a fájl
// verziószámát (EVFILE_VERSION) is emelni kell.
struct EventEntry {
  uint32_t uptimeSec;
  uint32_t epoch;     // valos ido (unix), 0 ha az NTP meg nem szinkronizalt
  uint16_t param;
  uint8_t code;
  uint8_t reserved;
};

constexpr uint8_t EVLOG_SIZE = 32;
// A magic EGYBEN VERZIOJELZO is. Az RTC NOINIT terulet a szoftveres resetet -
// tehat egy SOROS PORTON KERESZTULI FIRMWARE FRISSITEST is - tulel: az uj
// firmware a regi tartalmat talalja ott. Ha kozben az EventEntry elrendezese
// valtozott (mint amikor 8-rol 12 bajtra nott az epoch mezovel), a regi
// bejegyzesek UJ elrendezeskent ertelmezve szemetet adnanak - es a
// saveEventLog() ezt a szemetet meg ki is irna a fajlrendszerre.
//
// Ezert: HA AZ EventEntry VAGY AZ EVLOG_SIZE VALTOZIK, EZT A MAGIC-ET IS
// EMELNI KELL. Az uj firmware igy ervenytelennek latja a regi naplot, es
// tiszta lappal indul - egyetlen bootnyi naplo elvesztese az ara, cserebe
// nincs hamis diagnosztika.
constexpr uint32_t EVLOG_MAGIC = 0x42415A4DUL;  // "BAZM" (v2: 12 bajtos bejegyzes)
extern RTC_NOINIT_ATTR uint32_t rtcEvMagic;
extern RTC_NOINIT_ATTR uint32_t rtcEvNext;   // következő írási pozíció (monoton nő)
extern RTC_NOINIT_ATTR EventEntry rtcEvents[EVLOG_SIZE];

// --- A naplo mentese LittleFS-re -------------------------------------------
//
// MIERT? Az RTC naplo az aramszunetet NEM eli tul - epp azt a hibat nem, ami
// utan a leginkabb tudni akarnank, mi tortent elotte. Ezert a program a
// FONTOS pillanatokban kiirja a naplot a fajlrendszerre is: router reset
// elott, AP modba valtas elott, es az 1 oras alvas elott. Ezek azok a
// pontok, ahol vagy hosszabb ido kovetkezik, vagy az eszkoz beavatkozik -
// mindketto olyan, amit egy kesobbi vizsgalat latni akar.
//
// A FAJL SAJAT, NAGYOBB GYURU - NEM az RTC naplo pillanatkepe.
//
// KORABBAN pillanatkep volt: minden mentes FELULIRTA a fajlt az RTC gyuru
// aktualis tartalmaval. Ez egy esetben pontosan a legrosszabbat tette. Egy
// ARAMSZUNET utan ugyanis az RTC (NOINIT) terulet torlodik, tehat a
// rtcSavedEvNext is nullazodik - es az elso mentes egyetlen friss, meg
// idobelyeg nelkuli BOOT bejegyzest irt a korabbi 32 helyere. Merve:
// 404 bajt / 32 bejegyzes -> 32 bajt / 1 bejegyzes. Vagyis EPP AZ AZ ESEMENY
// tuntette el a tartos naplot, amiert az egyaltalan letezik. (Merve: NV20.)
//
// MOSTANTOL a mentes HOZZAFUZ: a fajl megorzi a regi tartalmat, es a vegere
// keruinek azok az RTC bejegyzesek, amik a legutobbi sikeres mentes ota
// keletkeztek (rtcSavedEvNext). A fajl igy a HOSSZU tortenet, az RTC gyuru
// pedig csak az azota eltelt ido - a ketto egyutt teljes.
//
// MIERT 128? A fajl 128 * 12 + 20 = 1556 bajt, a LittleFS particio 512 KB -
// a meret nem szempont. A korlat a MENTES verme: az osszefuzeshez egy
// EVFILE_SIZE meretu puffer kell a loop task vermen (1536 bajt a ~8 KB-bol).
// A 128 igy negyszerese az RTC gyurunek - eleg ahhoz, hogy tobb aramszunet
// tortenete is megmaradjon -, es meg boven belefer. A /log oldal EZT A
// PUFFERT NEM hasznalja: az async_tcp task verme szuk, ezert ott a fajl
// FOLYAMKENT olvasodik (lasd loadEventLogRange).
constexpr uint16_t EVFILE_SIZE = 128;
constexpr char evLogPath[] = "/evlog.bin";
constexpr uint32_t EVFILE_MAGIC = 0x42415A46UL;   // "BAZF"
// A VERZIO NEM VALTOZIK. Az elrendezes ugyanaz maradt - csak a "count" mezo
// vehet fel nagyobb erteket. Egy REGI (max 32 bejegyzeses) fajlt az uj
// firmware valtozatlanul beolvas, es az elso mentesnel egyszeruen kiegesziti:
// a firmware-frissites igy nem dob el tortenetet.
constexpr uint16_t EVFILE_VERSION = 1;

// A fajl fejlece. Fix meretu, es a struktura elrendezese a formatum resze.
struct EvFileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t count;        // hany bejegyzes kovetkezik (0..EVFILE_SIZE)
  uint32_t evNextAtSave; // az rtcEvNext erteke a mentes pillanataban
  uint32_t savedEpoch;   // mikor mentettuk (0 ha nem volt NTP)
  uint32_t savedUptime;  // uptime a mentes pillanataban
};

// Az rtcEvNext erteke a legutobbi SIKERES mentesnel. Ket dolgot ad:
//  - nem irunk feleslegesen (ha azota nem tortent esemeny, nincs mit menteni),
//  - es igy a flash kopasa a tenyleges esemenyekhez igazodik.
extern RTC_NOINIT_ATTR uint32_t rtcSavedEvNext;

// --- Valos ido (NTP) -------------------------------------------------------
//
// MIERT KELL? A naplo eddig csak uptime belyeget hordozott, ami minden
// indulaskor nullarol kezd - ket bootolas esemenyei igy nem rendezhetok
// egymashoz. A LittleFS-re mentett naplo ertelmezesehez viszont epp ez kell:
// melyik a frissebb, a fajlban levo vagy az RTC-ben levo?
//
// A szinkronizalas NEM BLOKKOL: a configTzTime() csak elinditja az SNTP
// klienst, a valasz a hatterben erkezik. Amig nem erkezett meg, az epoch mezo
// 0 marad - a naplo attol meg mukodik, csak uptime-ot mutat.
constexpr char NTP_SERVER[] = "hu.pool.ntp.org";
// Magyarorszag: CET/CEST, a nyari idoszamitas valtasaival egyutt (POSIX TZ).
constexpr char NTP_TZ[] = "CET-1CEST,M3.5.0,M10.5.0/3";
// Ennel korabbi ertek nem lehet valodi szinkron (2025-01-01). A rendszerora
// szinkron nelkul 1970-bol indul, tehat egy egyszeru also korlat elegendo.
constexpr uint32_t NTP_MIN_VALID_EPOCH = 1735689600UL;

// Az oraszinkron gondozasa. A loop()-bol hivjuk, minden uzemmodban. A
// szinkron INDITASA (startNtp) es az ora leolvasasa (nowEpoch) a modul
// belugye: kivulrol csak ez a harom fuggveny latszik.
void ensureNtp();

// Mit jelent ennek a modulnak egy BEKAPCSOLAS? Azt, hogy a RAM-beli jelzoi
// alaphelyzetbe kerulnek (az RTC memoriaban levo naplo viszont NEM - epp az a
// lenyege, hogy tulelje). Valodi eszkozon ezt a C futtatokornyezet vegzi el a
// statikus kezdoertekekkel, ezert a program maga sosem hivja. A host tesztek
// viszont EGY processzen belul tobb bekapcsolast is modelleznek: nekik kell
// egy nevesitett belepesi pont ahhoz, amit a hardver magatol megtesz.
void ntpResetForColdBoot();
// "EEEE-HH-NN OO:PP:MM" alakra formaz. Hamis, ha nincs ervenyes ido.
bool formatEpoch(uint32_t epoch, char* out, size_t outSize);

// --- A naplo ---------------------------------------------------------------

// Esemeny rogzitese a korpufferbe. Nem allokal, nem blokkol. KET TASKBOL is
// hivhato (a loop task es az async_tcp task) - a pozicio leptetese es a slot
// irasa egyutt atomi.
void logEvent(EventCode code, uint16_t param);

// Emberi nev egy esemenykodhoz, illetve egy reset okhoz (a /log oldalhoz).
const char* eventName(uint8_t code);
const char* resetReasonName(esp_reset_reason_t r);

// Igaz, ha a naplo LEGUTOLSO bejegyzese mar ez a kod. A sorozatok elleni
// spam-vedelem kozos alapja.
bool lastEventWas(uint8_t code);

// Igaz, ha a naplo legutobbi ket bejegyzese mar pontosan ez a beragadt-gomb
// kor (BOOT, majd STUCK BUTTON ugyanazzal a gombbal).
bool stuckCycleAlreadyLogged(uint16_t which);

// --- Mentes es visszatoltes ------------------------------------------------

// A naplo kiirasa a fajlrendszerre. A "reason" csak a soros portra megy.
bool saveEventLog(const char* reason);
// A mentett naplo FEJLECENEK beolvasasa es ellenorzese.
bool loadEventLogHeader(EvFileHeader& fej);
// A bejegyzesek beolvasasa a mar ellenorzott fejlec alapjan, KIEGYENESITVE
// (a legregebbitol a legujabbig), a puffer elejere.
bool loadEventLogEntries(const EvFileHeader& fej, EventEntry* ki);

// A fajl egy SZAKASZANAK beolvasasa - a folyamkent valo feldolgozashoz.
// A "tol" a legregebbi bejegyzestol szamitott 0-alapu index, a "db" pedig a
// kert darabszam. A hivo puffere legalabb "db" elemu kell legyen.
//
// MIERT KELL EZ A loadEventLogEntries() MELLE? Mert a /log oldal az async_tcp
// taskon fut, aminek a verme veges. Egy 128 elemu fajlhoz ott 1536 bajtos
// puffer kellene - ez a fuggveny viszont nyolcasaval is beolvashato, 96
// bajtbol. A loadEventLogEntries() a mentes utjan marad, ahol a loop task
// nagyobb verme all rendelkezesre.
bool loadEventLogRange(const EvFileHeader& fej, uint32_t tol, uint16_t db,
                       EventEntry* ki);

// MINDKET naplo kiirasa a soros portra: eloszor a fajlbol (a hosszu tortenet),
// utana az RTC gyurubol az, ami meg nincs a fajlban. A soros "LOG" parancs
// hivja - lasd a fomodul serialCommands() fuggvenyet.
void printEventLogs();
