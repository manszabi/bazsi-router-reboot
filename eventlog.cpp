#include "eventlog.h"

#include <LittleFS.h>
#include <WiFi.h>
#include <string.h>
#include <time.h>

#include "app_hooks.h"
#include "configstore.h"
#include "sync.h"

RTC_NOINIT_ATTR uint32_t rtcEvMagic;
RTC_NOINIT_ATTR uint32_t rtcEvNext;   // következő írási pozíció (monoton nő)
RTC_NOINIT_ATTR EventEntry rtcEvents[EVLOG_SIZE];
RTC_NOINIT_ATTR uint32_t rtcSavedEvNext;

// A rendszerora aktualis erteke, ha az NTP mar szinkronizalt - kulonben 0.
// Nem blokkol, nem allokal, es szinkron nelkul is biztonsagos.
static uint32_t nowEpoch() {
  const time_t t = time(nullptr);
  if (t < (time_t)NTP_MIN_VALID_EPOCH) {
    return 0;
  }
  return (uint32_t)t;
}

// Elindult-e mar az oraszinkron a MOSTANI kapcsolaton?
static bool ntpStarted = false;
// Bejelentettuk-e mar a soros porton ebben a bootban? (Lasd startNtp().)
static bool ntpAnnounced = false;

// Mit jelent ennek a modulnak egy BEKAPCSOLAS? A RAM-beli jelzoi alaphelyzetbe
// kerulnek (az RTC memoriaban levo naplo viszont NEM - epp az a lenyege, hogy
// tulelje). Valodi eszkozon ezt a C futtatokornyezet vegzi el, ezert a program
// maga sosem hivja; a host tesztek viszont EGY processzen belul tobb
// bekapcsolast is modelleznek. Lasd a header reszletesebb indoklasat.
void ntpResetForColdBoot() {
  ntpStarted = false;
  ntpAnnounced = false;
}

// Az SNTP kliens elinditasa. CSAK elinditja: a valasz a hatterben erkezik, a
// hivas nem var ra. Tobbszor is hivhato (ujracsatlakozasnal), a kliens
// ujraindul. A rendszeroraval egyutt a nyari idoszamitas kezelese is beall.
//
// FONTOS: a valos ido a DEEP SLEEPET TULELI. Az esp_timer (es igy a millis())
// ebredeskor nullarol indul, a gettimeofday() alapu rendszerora viszont az RTC
// orabol jon, tehat egy 1 oras alvas utan is jo idot mutat - epp ezert
// hasznalhato a naplo bejegyzesek rendezesere bootolasokon at.
static void startNtp() {
  configTzTime(NTP_TZ, NTP_SERVER);
  // A KIIRAS CSAK AZ ELSO ALKALOMMAL. Az SNTP klienst minden
  // ujracsatlakozasnal ujra kell inditani (a disconnect a netifet is
  // lebontja), egy PISLAKOLO kapcsolat viszont masodpercenkent tobbszor is
  // ad ilyen atmenetet - a kiirast tehat ugyanaz a spam-vedelmi szabaly koti,
  // mint a WIFI LOST sorokat: csak a sorozat elso tagja beszel.
  // (Merve: LOG4, ami a soros sor/perc erteket is meri.)
  //
  // A jelzo FAJL-SZINTU, nem fuggveny-szintu static - ugyanaz a szabaly, mint
  // a heap felugyelet orainal: a sketch per-boot allapota egy helyen legyen
  // visszaallithato (a teszt-harness hidegindulasa igy tudja nullazni).
  if (ntpAnnounced) {
    return;
  }
  ntpAnnounced = true;
  printUptime();
  Serial.print("NTP inditva: ");
  Serial.println(NTP_SERVER);
}

// Az oraszinkron gondozasa: elinditja, AMINT van halozat - barmelyik uton is
// jott letre a kapcsolat.
//
// MIERT NEM AZ initWiFi()-BOL? Mert nem minden kapcsolat rajta keresztul jon
// letre. Harom ag kerulte volna meg:
//   - az initWiFi() "mar csatlakozva vagyunk" korai visszaterese,
//   - a handleFirstStart() korai kilepese (a proba igazolta a kapcsolatot,
//     tehat nincs mit ujracsatlakoztatni) - ez a LEGGYAKORIBB helyreallasi
//     ut aramszunet utan,
//   - es a FAILURE_STATE RESET_DELAY korai kilepese.
// Mindharomban elmaradt volna a szinkron, es a naplo epoch mezoje vegig 0
// maradt volna. Ezert a gondozas a loop()-bol fut, egyetlen helyrol: egy uj
// kapcsolati ut sem tudja elfelejteni. (Merve: NV10.)
//
// A jelzot a kapcsolat elvesztesekor toroljuk, mert a WiFi.disconnect(true) a
// netifet is lebontja - ujracsatlakozas utan az SNTP klienst ujra kell
// inditani.
void ensureNtp() {
  if (WiFi.status() != WL_CONNECTED) {
    ntpStarted = false;
    return;
  }
  if (ntpStarted) {
    return;
  }
  ntpStarted = true;
  startNtp();
}

// Egy idopont ember altal olvashato alakja. Ha nincs valos ido, a hivo
// dontse el, mit ir helyette - ez a fuggveny csak akkor ad true-t, ha tenyleg
// van mit formazni.
bool formatEpoch(uint32_t epoch, char* out, size_t outSize) {
  if (epoch < NTP_MIN_VALID_EPOCH || outSize < 20) {
    return false;
  }
  const time_t t = (time_t)epoch;
  struct tm tmv;
  // VEDELMI AG, a lefedettsegben szandekosan feher. Az "epoch" uint32_t, tehat
  // legfeljebb 4 294 967 295 masodperc = 2106. februar - azt a localtime_r()
  // minden tamogatott platformon ertelmezni tudja, tehat ez az ag ma nem
  // erheto el. Megis itt marad: ha a mezo valaha 64 bitesre no (az EventEntry
  // epoch mezoje mar most is a fajlformatum resze), a hibakezeles keszen all,
  // es a /log oldal nem egy inicializalatlan pufferbol olvasna.
  if (localtime_r(&t, &tmv) == nullptr) {
    return false;
  }
  strftime(out, outSize, "%Y-%m-%d %H:%M:%S", &tmv);
  return true;
}

// Esemény rögzítése a körpufferbe. Nem allokál, nem blokkol.
//
// Két task is hív: a loop task mellett az async_tcp task is (a POST kezelő
// CONFIG_SAVED bejegyzése). A pozíció léptetése és a slot írása együtt nem
// atomi, ezért kritikus szakasz védi - enélkül két egyidejű hívás ugyanabba
// a slotba írhatna, vagy egy bejegyzés elveszne. A szakasz rövid (a memset
// csak a legelső híváskor fut), a megszakítás-tiltás belefér.
// Az evLogMux a sync.h-ban all: a naplo ES a ket task kozotti osztott
// allapot ugyanazt a rovid kritikus szakaszt hasznalja.
void logEvent(EventCode code, uint16_t param) {
  // A KET IDOBELYEGET A KRITIKUS SZAKASZON KIVUL keszitjuk el.
  //
  // MIERT? A portENTER_CRITICAL a C3-on letiltja a megszakitasokat, tehat
  // odabent minden extra munka kozvetlen koltseg. Az uptime egy 64 bites
  // osztas, a nowEpoch() pedig time()-ot hiv - az ESP-IDF-ben ez a
  // rendszerora sajat zarjat is megfoghatja. Idegen zarat felvenni letiltott
  // megszakitasok mellett nem az a minta, amit egy naplozo fuggvenytol
  // varunk; a struktura sajat kommentje is azt allitja, hogy a szakasz
  // "rovid". Igy viszont odabent tenyleg csak ertekadasok maradnak.
  //
  // Az idobelyegek nehany mikromasodperccel korabbrol szarmaznak - ez a
  // masodperces felbontas mellett nem szamit.
  const uint32_t uptimeSec = (uint32_t)(esp_timer_get_time() / 1000000);
  // Ha az NTP mar szinkronizalt, a valos idot is eltesszuk. Enelkul csak
  // uptime van, ami minden indulaskor nullarol kezd - ket bootolas esemenyei
  // igy nem rendezhetok egymashoz.
  const uint32_t epoch = nowEpoch();

  portENTER_CRITICAL(&evLogMux);
  if (rtcEvMagic != EVLOG_MAGIC) {
    rtcEvMagic = EVLOG_MAGIC;
    rtcEvNext = 0;
    rtcSavedEvNext = 0;
    memset(rtcEvents, 0, sizeof(rtcEvents));
  }
  EventEntry& e = rtcEvents[rtcEvNext % EVLOG_SIZE];
  e.uptimeSec = uptimeSec;
  e.epoch = epoch;
  e.code = (uint8_t)code;
  e.param = param;
  e.reserved = 0;
  rtcEvNext++;
  portEXIT_CRITICAL(&evLogMux);
}

// Az indulás okának EMBERI neve. A /log oldalon eddig a nyers enum-szám
// szerepelt ("Utolso indulas oka: 8"), amihez a felhasználónak az ESP-IDF
// fejlécét kellett volna kikeresnie - épp azt a diagnózist nehezítve, amiért
// az oldal egyáltalán van. A számot zárójelben megtartjuk, hogy egy
// hibajelentés továbbra is egyértelmű legyen.
const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:    return "bekapcsolas / aramtalanitas";
    case ESP_RST_EXT:        return "kulso reset lab";
    case ESP_RST_SW:         return "szoftveres ujrainditas (ESP.restart)";
    case ESP_RST_PANIC:      return "PANIC - a program osszeomlott";
    case ESP_RST_INT_WDT:    return "megszakitas-watchdog";
    case ESP_RST_TASK_WDT:   return "TASK WATCHDOG - a loop megallt";
    case ESP_RST_WDT:        return "egyeb watchdog";
    case ESP_RST_DEEPSLEEP:  return "ebredes deep sleepbol";
    case ESP_RST_BROWNOUT:   return "BROWNOUT - leesett a tapfeszultseg";
    case ESP_RST_CPU_LOCKUP: return "CPU lefagyas";
    // Az IDF 5.x tovabbi okai. Ketto koztuk EPP EZEN A LAPKAN es EPP EZNEL A
    // KESZULEKNEL szamit igazan:
    //   USB        - a XIAO ESP32-C3 natív USB-t hasznal, tehat egy firmware
    //                feltoltes vagy egy soros monitor megnyitasa IDE fut be.
    //                "ismeretlen (11)"-kent ez zavarba ejto volt.
    //   PWR GLITCH - tapfeszultseg-tuske. Egy olyan eszkoznel, aminek a dolga
    //                EPPEN a halozati aram kapcsolgatasa egy relevel, ez az
    //                egyik legfontosabb diagnosztikai jelzes: sajat magat
    //                zavarja-e meg a kapcsolas.
    case ESP_RST_SDIO:       return "SDIO reset";
    case ESP_RST_USB:        return "USB reset (feltoltes vagy soros monitor)";
    case ESP_RST_JTAG:       return "JTAG reset (hibakereso)";
    case ESP_RST_EFUSE:      return "eFuse hiba";
    case ESP_RST_PWR_GLITCH: return "TAPFESZULTSEG-TUSKE";
    default:                 return "ismeretlen";
  }
}

const char* eventName(uint8_t code) {
  switch (code) {
    // A default ag vedelmi celu: az L6 minden letezo kodot ellenoriz, tehat a
    // "?" a host tesztekben sosem all elo. Egy JOVOBELI kod viszont igy nem
    // tud olvashatatlan lapot csinalni, csak egy kerdojelet.
    case EV_BOOT: return "BOOT";
    case EV_WIFI_OK: return "WIFI OK";
    case EV_WIFI_LOST: return "WIFI LOST";
    case EV_TEST_FAIL: return "TEST FAIL";
    case EV_ROUTER_RESET: return "ROUTER RESET";
    case EV_AP_MODE: return "AP MODE";
    case EV_CONFIG_SAVED: return "CONFIG SAVED";
    case EV_SLEEP: return "SLEEP";
    case EV_FATAL: return "FATAL";
    case EV_WDT_RESET: return "WDT RESET";
    case EV_STUCK_BUTTON: return "STUCK BUTTON";
    case EV_GW_UNREACHABLE: return "GW UNREACH";
    case EV_LOW_HEAP: return "LOW HEAP";
    case EV_HEAP_RESTART: return "HEAP RESTART";
    default: return "?";
  }
}

// Igaz, ha a napló LEGUTOLSÓ bejegyzése már ez a kód. A sorozatok elleni
// spam-védelem közös alapja: a 32 bejegyzéses körpuffer néhány másodperc
// alatt kiszorítaná a kivizsgálandó eseményeket (BOOT, ROUTER RESET, FATAL),
// ha egy ismétlődő állapot minden körben új sort írna.
//
// A mux ugyanazért kell, mint a logEvent()-ben: a naplóba az async_tcp task
// is ír (a POST kezelő CONFIG_SAVED sora), tehát a pozíció és a slot együtt
// nem olvasható atomian.
bool lastEventWas(uint8_t code) {
  bool egyezik = false;
  portENTER_CRITICAL(&evLogMux);
  if (rtcEvMagic == EVLOG_MAGIC && rtcEvNext > 0) {
    egyezik = (rtcEvents[(rtcEvNext - 1) % EVLOG_SIZE].code == code);
  }
  portEXIT_CRITICAL(&evLogMux);
  return egyezik;
}

// Igaz, ha a napló legutóbbi két bejegyzése már pontosan ez a beragadt-gomb
// kör (BOOT, majd STUCK BUTTON ugyanazzal a gombbal) - vagyis a mostani
// ébredés csak a 60 mp-es alvás-ébredés kör ismétlése.
//
// A MUX itt is kell, ugyanazert, mint a lastEventWas()-ban. MA ez a fuggveny
// csak a setup()-bol fut, ahol a webszerver meg el sem indult, tehat masik iro
// nincs - de ezt SEHOL nem mondja ki semmi, es a szomszedos lastEventWas()
// kulon kommentben indokolja, miert zarol. Ket egymas melletti, azonos dolgot
// olvaso fuggveny, ketfele szabaly szerint: a kovetkezo olvaso nem tudna
// eldonteni, melyik a helyes. Az ar egyetlen rovid szakasz, bootolasonkent
// egyszer.
bool stuckCycleAlreadyLogged(uint16_t which) {
  bool egyezik = false;
  portENTER_CRITICAL(&evLogMux);
  if (rtcEvMagic == EVLOG_MAGIC && rtcEvNext >= 2) {
    const EventEntry& last = rtcEvents[(rtcEvNext - 1) % EVLOG_SIZE];
    const EventEntry& prev = rtcEvents[(rtcEvNext - 2) % EVLOG_SIZE];
    egyezik = last.code == EV_STUCK_BUTTON && last.param == which
              && prev.code == EV_BOOT;
  }
  portEXIT_CRITICAL(&evLogMux);
  return egyezik;
}

// A napló kiírása a fájlrendszerre.
//
// A ZAR. A muvelet ATOMIKUSAN szerzi meg a konfiguraciós zárat, ugyanazt,
// amit a webes mentés és a wifireset gomb használ. Amíg a zár a miénk:
//   - nem alszik el az eszköz és nem indul újra (a leállási út megvárja),
//   - a gombok nem szólnak közbe,
//   - és másik fájlírás sem indulhat.
// Ha a zár épp foglalt, NEM várunk rá: a mentés kimarad. Ez helyes döntés -
// a napló diagnosztika, nem szabad miatta blokkolni egy fontos műveletet
// (router reset, alvás), és a következő fontos pillanatban úgyis próbáljuk.
//
// A zárat a végén FELOLDJUK - eltérően a leállási úttól, ami már nem tér vissza.
// A fajl egy SZAKASZANAK beolvasasa. A "tol" a legregebbi bejegyzestol
// szamitott 0-alapu index.
//
// SAJAT HATARELLENORZES, a hivotol fuggetlenul - ugyanaz az elv, mint a
// loadEventLogEntries()-nel: a fej.count FAJLBOL szarmazo ertek.
bool loadEventLogRange(const EvFileHeader& fej, uint32_t tol, uint16_t db,
                       EventEntry* ki) {
  if (fej.count == 0 || fej.count > EVFILE_SIZE) {
    return false;
  }
  if (db == 0 || tol > fej.count || (uint32_t)db > (uint32_t)fej.count - tol) {
    return false;   // a kert szakasz kilogna a fajlbol
  }
  if (!filesystemReady() || !LittleFS.exists(evLogPath)) {
    return false;
  }
  File f = LittleFS.open(evLogPath, "r");
  if (!f) {
    return false;
  }
  // A stream nem kereshet, ezert atolvassuk (es eldobjuk) a fejlecet es a
  // kihagyando bejegyzeseket. Fix meretu, apro pufferrel: ez a fuggveny epp
  // azert van, hogy a hivonak NE kelljen nagy puffert tartania.
  EventEntry szemet;
  EvFileHeader eldobando;
  if (f.read((uint8_t*)&eldobando, sizeof(eldobando)) != sizeof(eldobando)) {
    f.close();
    return false;
  }
  for (uint32_t i = 0; i < tol; i++) {
    if (f.read((uint8_t*)&szemet, sizeof(szemet)) != sizeof(szemet)) {
      f.close();
      return false;
    }
  }
  const size_t kell = sizeof(EventEntry) * db;
  const size_t olvasott = f.read((uint8_t*)ki, kell);
  f.close();
  return olvasott == kell;
}

// MINDKET naplo a soros portra. A soros "LOG" parancs hivja.
//
// MIERT KELL, HA VAN /log OLDAL? Mert az AP portál csak akkor fut, ha az
// eszkoz konfig modba kerult - egy normalisan mukodo eszkoztol viszont a
// soros kabel az egyetlen ut a naplohoz. Es mert a ket forras EGYUTT a
// teljes tortenet: a fajl a hosszu mult, az RTC gyuru az azota eltelt ido.
static void printOneEntry(uint16_t sorszam, const EventEntry& e) {
  char ido[24];
  if (e.epoch >= NTP_MIN_VALID_EPOCH && formatEpoch(e.epoch, ido, sizeof(ido))) {
    Serial.print(ido);
  } else {
    Serial.print("        -          ");
  }
  Serial.printf("  %3u  %3u:%02u:%02u  %-14s %u\n",
                (unsigned)sorszam,
                (unsigned)(e.uptimeSec / 3600), (unsigned)((e.uptimeSec % 3600) / 60),
                (unsigned)(e.uptimeSec % 60), eventName(e.code), (unsigned)e.param);
}

void printEventLogs() {
  Serial.println();
  Serial.println("=== ESEMENYNAPLO ===");
  Serial.println("valos ido            sor   uptime    esemeny        param");

  // 1. A FAJL - a hosszu tortenet. Nyolcasaval olvassuk: a puffer igy 96
  //    bajt, nem 1536. (A soros parancs a loop taskon fut, de a szuk
  //    keret elve itt is ervenyes.)
  uint16_t sorszam = 0;
  EvFileHeader fej;
  const bool vanFajl = loadEventLogHeader(fej);
  if (vanFajl) {
    Serial.printf("-- fajl (%s): %u bejegyzes\n", evLogPath, (unsigned)fej.count);
    EventEntry darab[8];
    uint32_t tol = 0;
    while (tol < fej.count) {
      const uint16_t db = (uint16_t)((fej.count - tol < 8) ? (fej.count - tol) : 8);
      if (!loadEventLogRange(fej, tol, db, darab)) {
        Serial.println("   (a fajl olvasasa megszakadt)");
        break;
      }
      for (uint16_t i = 0; i < db; i++) {
        printOneEntry(++sorszam, darab[i]);
      }
      tol += db;
    }
  } else {
    Serial.println("-- fajl: nincs, vagy nem olvashato");
  }

  // 2. AZ RTC GYURU - csak ami MEG NINCS a fajlban. Igy nincs ketszerezes,
  //    es pontosan latszik, mi tortent a legutobbi mentes ota.
  uint32_t evTotal, mentettEddig;
  EventEntry masolat[EVLOG_SIZE];
  portENTER_CRITICAL(&evLogMux);
  evTotal = (rtcEvMagic == EVLOG_MAGIC) ? rtcEvNext : 0;
  mentettEddig = rtcSavedEvNext;
  memcpy(masolat, rtcEvents, sizeof(masolat));
  portEXIT_CRITICAL(&evLogMux);

  // MEDDIG BIZHATUNK A "MAR MENTETTUK" JELZOBEN?
  //
  // A ket forras sorrendje SZERKEZETI, nem idobelyeg-alapu: a fajl a regebbi
  // (oldest->newest kiegyenesitve), az RTC uj bejegyzesei pedig definicio
  // szerint utana kovetkeznek. Ezert nem is kell idobelyeg a rendezeshez -
  // es ez jo, mert NTP nelkul nem is lenne.
  //
  // A jelzo viszont csak akkor mond igazat, ha a FAJL TENYLEG MEGVAN. Ha
  // elveszett (LittleFS ujraformazas, torolt fajl, flash-hiba), akkor azok a
  // bejegyzesek, amikrol azt hisszuk, hogy "mar mentve vannak", SEHOL nem
  // latszanak - pedig az RTC gyuruben ott vannak. Merve: 13 meglevo
  // bejegyzesbol 3 latszott. (GAP teszt.)
  //
  // Es a fajl SAJAT allitasa (evNextAtSave) is szamit: aramszunet utan a
  // rtcSavedEvNext nullarol indul, mikozben a fajl egy REGEBBI "eletbol" valo
  // nagyobb szamot hordoz. A kettobol a KISEBB a helyes alap - igy sem
  // elrejteni, sem ketszerezni nem tudunk.
  const uint32_t alap = vanFajl
      ? (mentettEddig < fej.evNextAtSave ? mentettEddig : fej.evNextAtSave) : 0;
  const uint32_t keletkezett = (evTotal > alap) ? (evTotal - alap) : 0;
  const uint16_t ujDb = (uint16_t)(keletkezett < EVLOG_SIZE ? keletkezett : EVLOG_SIZE);
  Serial.printf("-- RTC (a mentes ota): %u bejegyzes\n", (unsigned)ujDb);
  for (uint16_t i = 0; i < ujDb; i++) {
    printOneEntry(++sorszam, masolat[(evTotal - ujDb + i) % EVLOG_SIZE]);
  }
  Serial.println("=== NAPLO VEGE ===");
  Serial.println();
}

bool saveEventLog(const char* reason) {
  if (!filesystemReady()) {
    return false;   // nincs hova irni; ez nem hiba, csak nincs mentes
  }

  // Nincs mit menteni? Akkor a flasht sem koptatjuk. Ez nem optimalizacio,
  // hanem a kopas es a tenyleges esemenyek osszehangolasa.
  // A ket erteket EGYUTT olvassuk ki: az "van-e uj esemeny?" kerdes a kettejuk
  // VISZONYA. Kulon olvasva a logEvent() magic-inicializalasa (ami mindkettot
  // nullazza) beeshetne koze, es egy elavult evTotal-t hasonlitanank egy friss
  // rtcSavedEvNext-hez. Ugyanaz az elv, mint a sync modul hatarido-jelzo
  // paranal - csak ott a kovetkezmeny sulyosabb.
  uint32_t evTotal;
  uint32_t mentettEddig;
  portENTER_CRITICAL(&evLogMux);
  evTotal = (rtcEvMagic == EVLOG_MAGIC) ? rtcEvNext : 0;
  mentettEddig = rtcSavedEvNext;
  portEXIT_CRITICAL(&evLogMux);
  if (evTotal == 0 || evTotal == mentettEddig) {
    return false;
  }

  if (!beginConfigWrite()) {
    printUptime();
    Serial.println("A naplo mentese kimarad: eppen mas fajliras folyik.");
    return false;
  }

  // ---------------------------------------------------------------------
  // OSSZEFUZES, nem felulíras.
  //
  // A fajl a HOSSZU tortenet, az RTC gyuru pedig csak a legutobbi sikeres
  // mentes ota eltelt ido. Az uj fajl tartalma tehat:
  //     [a regi fajl vege] + [az RTC uj bejegyzesei]
  // a legregebbieket ejtve, ha nem fernek bele az EVFILE_SIZE-ba.
  //
  // Enelkul egy aramszunet utani elso mentes egyetlen friss bejegyzest irt a
  // korabbi 32 helyere - lasd az indoklast az eventlog.h-ban.
  // ---------------------------------------------------------------------

  // Pillanatkep a mux alatt, ugyanugy, mint a /log oldalon: a naploba az
  // async_tcp task is ir, tehat a pozicio es a tartalom egyutt nem olvashato
  // atomian.
  EventEntry masolat[EVLOG_SIZE];
  uint32_t evNextMost;
  portENTER_CRITICAL(&evLogMux);
  evNextMost = rtcEvNext;
  memcpy(masolat, rtcEvents, sizeof(masolat));
  portEXIT_CRITICAL(&evLogMux);

  // Hany RTC bejegyzes UJ a legutobbi mentes ota? Ha kozben tobb mint
  // EVLOG_SIZE esemeny tortent, a gyuru korbefordult: csak az utolso
  // EVLOG_SIZE mentheto, a tobbi menthetetlenul elveszett.
  const uint32_t keletkezett = evNextMost - mentettEddig;
  const uint16_t ujDb = (uint16_t)(keletkezett < EVLOG_SIZE ? keletkezett : EVLOG_SIZE);

  // A MUNKAPUFFER. A loop task vermen all (~8 KB), NEM az async_tcp-en.
  EventEntry uj[EVFILE_SIZE];
  uint16_t osszes = 0;

  // 1. A regi fajl vege - annyi, amennyi az uj bejegyzesek mellett elfer.
  EvFileHeader regi;
  if (loadEventLogHeader(regi)) {
    const uint16_t ferohely = (uint16_t)(EVFILE_SIZE - ujDb);
    const uint16_t megtart = regi.count < ferohely ? regi.count : ferohely;
    const uint32_t ettol = (uint32_t)(regi.count - megtart);   // a legregebbieket ejtjuk
    if (megtart > 0 && !loadEventLogRange(regi, ettol, megtart, uj)) {
      // A regi fajl olvashatatlan. NEM ez az utolso szo: az uj bejegyzesek
      // igy is mentheto k, csak az elozmeny vesz el - ugyanaz, mint a regi
      // (felulíro) viselkedes. Szolunk rola, mert ez flash-hibara utal.
      Serial.println("- a korabbi naplofajl nem olvashato, csak az ujakat mentjuk");
    } else {
      osszes = megtart;
    }
  }

  // 2. Az RTC uj bejegyzesei, a legregebbitol a legujabbig kiegyenesitve.
  for (uint16_t i = 0; i < ujDb; i++) {
    const uint32_t sorszam = evNextMost - ujDb + i;
    uj[osszes + i] = masolat[sorszam % EVLOG_SIZE];
  }
  osszes = (uint16_t)(osszes + ujDb);

  EvFileHeader fej;
  fej.magic = EVFILE_MAGIC;
  fej.version = EVFILE_VERSION;
  fej.count = osszes;
  fej.evNextAtSave = evNextMost;
  fej.savedEpoch = nowEpoch();
  fej.savedUptime = (uint32_t)(esp_timer_get_time() / 1000000);

  printUptime();
  Serial.print("Naplo mentese a fajlrendszerre (");
  Serial.print(reason);
  Serial.println(")...");

  bool ok = false;
  File f = LittleFS.open(evLogPath, FILE_WRITE);
  if (!f) {
    Serial.println("- a naplofajl nem nyithato irasra");
  } else {
    // Egyetlen osszefuggo tomb: a kiegyenesitest mar az osszefuzes elvegezte.
    const size_t fejBytes = f.write((const uint8_t*)&fej, sizeof(fej));
    const size_t adatBytes = fej.count
        ? f.write((const uint8_t*)uj, sizeof(EventEntry) * fej.count) : 0;
    f.flush();
    f.close();
    const size_t vart = sizeof(fej) + sizeof(EventEntry) * fej.count;
    if (fejBytes + adatBytes != vart) {
      Serial.print("- rovid iras: ");
      Serial.print((unsigned)(fejBytes + adatBytes));
      Serial.print(" / ");
      Serial.print((unsigned)vart);
      Serial.println(" bajt (megtelt a fajlrendszer?)");
    } else {
      // VISSZAOLVASAS. Ugyanaz az elv, mint a writeConfigValue()-nal: a siker
      // nem az, hogy az iras nem panaszkodott, hanem hogy a tartalom tenyleg
      // ott van. Csak a fejlecet olvassuk vissza - ha az ep, a fajlmeret
      // pedig egyezik, a tartalom is kiirodott.
      File v = LittleFS.open(evLogPath, "r");
      if (!v) {
        Serial.println("- verify: a fajl nem nyithato olvasasra");
      } else {
        EvFileHeader vissza;
        const size_t olvasott = v.read((uint8_t*)&vissza, sizeof(vissza));
        const size_t meret = v.size();
        v.close();
        if (olvasott != sizeof(vissza) || meret != vart
            || vissza.magic != EVFILE_MAGIC || vissza.count != fej.count
            || vissza.evNextAtSave != fej.evNextAtSave) {
          Serial.println("- verify FAILED: a visszaolvasott fejlec nem egyezik");
        } else {
          ok = true;
        }
      }
    }
  }

  if (ok) {
    rtcSavedEvNext = fej.evNextAtSave;
    Serial.print("- naplo mentve, ");
    Serial.print((unsigned)fej.count);
    Serial.print(" bejegyzes (ebbol ");
    Serial.print((unsigned)ujDb);
    Serial.println(" uj)");
  } else {
    // A SIKERTELENSEG NEM VEGZETES. A naplo diagnosztika: ha nem sikerul
    // kiirni, az RTC-ben tovabbra is ott van, es a program dolga (router
    // reset, alvas) fontosabb. Csak szolunk rola.
    Serial.println("- a naplo mentese NEM sikerult, az RTC naplo ettol meg el");
  }
  endConfigWrite();   // a zar feloldasa - itt meg VISSZATERUNK
  return ok;
}

// A mentett naplo FEJLECENEK beolvasasa es ellenorzese. Igaz, ha a fajl
// letezik, ep, es van benne legalabb egy bejegyzes. Minden mas esetben (nincs
// fajl, ures fajl, rossz magic, csonka tartalom, ismeretlen verzio, a
// fejlecben igert darabszamnal rovidebb fajl) hamis - a hivo ilyenkor az RTC
// naplot hasznalja, kulon hibauzenet nelkul.
//
// MIERT KULON A FEJLEC ES A TARTALOM? Az async_tcp task verme veges, es 32
// bejegyzes mar 384 bajt. Ha itt is puffert kernenk, a /log kezelo EGYSZERRE
// ket ilyet tartana (az RTC pillanatkepet es a fajlet). Igy viszont eloszor
// eldontjuk a fejlecbol, melyik forras kell - es csak azt toltjuk be, EGY
// pufferbe.
bool loadEventLogHeader(EvFileHeader& fej) {
  if (!filesystemReady() || !LittleFS.exists(evLogPath)) {
    return false;
  }
  File f = LittleFS.open(evLogPath, "r");
  if (!f) {
    return false;
  }
  const size_t meret = f.size();
  if (meret < sizeof(EvFileHeader)
      || f.read((uint8_t*)&fej, sizeof(fej)) != sizeof(fej)) {
    f.close();   // ures vagy csonka fajl
    return false;
  }
  f.close();
  if (fej.magic != EVFILE_MAGIC || fej.version != EVFILE_VERSION
      || fej.count == 0 || fej.count > EVFILE_SIZE) {
    return false;
  }
  // A fejlec tobbet igerhet, mint amennyi tenyleg ott van (megtelt fajlrendszer,
  // felbeszakadt iras). Ezt MOST ellenorizzuk, hogy a betoltes mar biztos legyen.
  return meret >= sizeof(EvFileHeader) + sizeof(EventEntry) * fej.count;
}

// A bejegyzesek betoltese - csak akkor, ha a fejlec mar rendben volt.
bool loadEventLogEntries(const EvFileHeader& fej, EventEntry* ki) {
  // SAJAT HATARELLENORZES, nem csak a hivo szerzodesere hagyatkozva. A
  // fej.count egy FAJLBOL szarmazo ertek: egy serult vagy idegen kezzel irt
  // /evlog.bin barmit mondhat. A hivo (loadEventLogHeader) ma ellenorzi, de az
  // egy MASIK fuggveny - ha valaha kozvetlenul hivnak minket, egy 0xFFFF-es
  // count a hivo 384 bajtos vermet irna tul. Egy sor, es a modul a hivotol
  // fuggetlenul is biztonsagos.
  if (fej.count == 0 || fej.count > EVFILE_SIZE) {
    return false;
  }
  File f = LittleFS.open(evLogPath, "r");
  if (!f) {
    return false;
  }
  EvFileHeader eldobando;    // a fejlecet atugorjuk (a stream nem kereshet)
  if (f.read((uint8_t*)&eldobando, sizeof(eldobando)) != sizeof(eldobando)) {
    f.close();
    return false;
  }
  const size_t kell = sizeof(EventEntry) * fej.count;
  const size_t olvasott = f.read((uint8_t*)ki, kell);
  f.close();
  return olvasott == kell;
}
