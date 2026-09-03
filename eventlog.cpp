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

  // Pillanatkep a mux alatt, ugyanugy, mint a /log oldalon: a naploba az
  // async_tcp task is ir, tehat a pozicio es a tartalom egyutt nem olvashato
  // atomian.
  EvFileHeader fej;
  EventEntry masolat[EVLOG_SIZE];
  portENTER_CRITICAL(&evLogMux);
  fej.evNextAtSave = rtcEvNext;
  memcpy(masolat, rtcEvents, sizeof(masolat));
  portEXIT_CRITICAL(&evLogMux);

  fej.magic = EVFILE_MAGIC;
  fej.version = EVFILE_VERSION;
  fej.count = (uint16_t)(fej.evNextAtSave < EVLOG_SIZE ? fej.evNextAtSave : EVLOG_SIZE);
  fej.savedEpoch = nowEpoch();
  fej.savedUptime = (uint32_t)(esp_timer_get_time() / 1000000);

  // A bejegyzeseket a LEGREGEBBITOL a legujabbig irjuk ki, kiegyenesitve -
  // igy az olvasonak nem kell tudnia, hol tartott a korpuffer. Ehhez NEM
  // masolunk egy ujabb 384 bajtos verempuffert: a korpuffer legfeljebb ket
  // OSSZEFUGGO szakaszra bomlik (a kezdoponttol a tomb vegeig, majd a tomb
  // elejetol), es ezt a kettot irjuk ki egymas utan. A loop task verme igy
  // 384 bajttal kevesebbet visz.
  const uint32_t elso = fej.evNextAtSave - fej.count;
  const uint16_t kezdet = (uint16_t)(elso % EVLOG_SIZE);
  const uint16_t elsoDb = (uint16_t)((kezdet + fej.count <= EVLOG_SIZE)
                                     ? fej.count : (EVLOG_SIZE - kezdet));
  const uint16_t masodikDb = (uint16_t)(fej.count - elsoDb);

  printUptime();
  Serial.print("Naplo mentese a fajlrendszerre (");
  Serial.print(reason);
  Serial.println(")...");

  bool ok = false;
  File f = LittleFS.open(evLogPath, FILE_WRITE);
  if (!f) {
    Serial.println("- a naplofajl nem nyithato irasra");
  } else {
    const size_t fejBytes = f.write((const uint8_t*)&fej, sizeof(fej));
    size_t adatBytes = 0;
    if (elsoDb) {
      adatBytes += f.write((const uint8_t*)&masolat[kezdet],
                           sizeof(EventEntry) * elsoDb);
    }
    if (masodikDb) {
      adatBytes += f.write((const uint8_t*)&masolat[0],
                           sizeof(EventEntry) * masodikDb);
    }
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
    Serial.println(" bejegyzes");
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
      || fej.count == 0 || fej.count > EVLOG_SIZE) {
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
  if (fej.count == 0 || fej.count > EVLOG_SIZE) {
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
