#include "webportal.h"

#include <AsyncTCP.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <string.h>
#include "esp_random.h"

#include "app_hooks.h"
#include "configstore.h"
#include "eventlog.h"
#include "limits_config.h"
#include "secret.h"
#include "strutil.h"
#include "sync.h"

static constexpr uint32_t RESTART_GRACE_MS = 2000;

// A POST két fázisa: előbb minden mező validálása ide, csak utána írás.
struct ConfigDraft {
  char ssid[SSID_MAX_LEN + 1]      = { 0 };
  char pass[PASS_MAX_LEN + 1]      = { 0 };
  char passEnc[SECRET_ENC_MAX + 1] = { 0 };

static_assert(sizeof(ConfigDraft::pass) - 1 <= PASS_MAX_LEN,
              "a ConfigDraft jelszo-mezoje nem lehet hosszabb PASS_MAX_LEN-nel");
static_assert(SECRET_ENC_MAX >= SECRET_PREFIX_LEN + 2 * (sizeof(ConfigDraft::pass) - 1),
              "a kodolt jelszo pufferenek MINDIG elegnek kell lennie - kulonben "
              "a parseConfigPost() 'nem fert a pufferbe' aga elerhetove valna");
  char ip[IPSTR_MAX_LEN + 1]       = { 0 };
  char gateway[IPSTR_MAX_LEN + 1]  = { 0 };
  bool outageAp     = false;
  bool outageApSet  = false;
  bool openNet      = false;
  bool ssidProvided = false;
  bool passSet      = false;
  bool ipSet        = false;
  bool gwSet        = false;
};

static AsyncWebServer server(80);

// Az állapotoldal külön szerver: sosem fut egyszerre az AP portállal.
static AsyncWebServer statusServer(80);
static bool statusHandlersSet = false;
static bool statusRunning = false;
constexpr uint16_t STATUS_EVENTS = 20;

constexpr size_t LOG_PAGE_FIX_BYTES = 2600;
constexpr size_t LOG_PAGE_ROW_BYTES = 120;
constexpr size_t LOG_STREAM_MIN = 3072;

const char PARAM_SSID[]    = "ssid";
const char PARAM_PASS[]    = "pass";
const char PARAM_IP[]      = "ip";
const char PARAM_GATEWAY[] = "gateway";
const char PARAM_OUTAGE_AP[] = "outageap";
const char PARAM_OPEN[] = "open";

#define KEEPALIVE_JS_LIT \
  "<script>f=()=>fetch('/ping?'+Date.now());setInterval(f,6e4);" \
  "document.onvisibilitychange=()=>document.hidden||f()</script>"
const char KEEPALIVE_JS[] = KEEPALIVE_JS_LIT;

const char FORM_HEAD[] =
  "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
  "<title>ESP Wi-Fi beallitas</title></head><body>"
  "<h2>ESP Wi-Fi beallitas</h2>"
  "<form action=\"/\" method=\"POST\">";
const char FORM_TAIL[] =
  "<small>Statikus IP-hez mindket cimmezot toltsd ki, csak IPv4. "
  "DHCP-hez hagyd mindkettot uresen.</small><br>"
  "<input type=\"submit\" value=\"Mentes\"></form>"
  "<p><a href=\"/log\">Diagnosztikai naplo</a></p>"
  KEEPALIVE_JS_LIT
  "</body></html>";

// Eszközönként egyedi AP jelszó: 12 karakter, 32 elemű, összetéveszthető jelek nélküli ábécé.
constexpr size_t AP_PASS_LEN = 12;
static_assert(AP_PASS_LEN >= 8 && AP_PASS_LEN <= 63, "AP jelszo: 8-63 karakter (WPA2)");
static const char kApPassAlphabet[] = "abcdefghjkmnpqrstuvwxyz23456789A";
static_assert(sizeof(kApPassAlphabet) - 1 == 32, "az abece pontosan 32 elemu (& 31)");
const char apPassPath[] = "/appass.txt";
static char apPass[PASS_MAX_LEN + 1] = { 0 };

static bool loadApPassword() {
  if (!filesystemReady()) {
    return false;
  }
  char raw[SECRET_ENC_MAX + 1];
  if (readSecretValue(LittleFS, apPassPath, raw, sizeof(raw)) != CONFIG_OK) {
    return false;
  }
  decodeSecretInPlace(raw);
  const size_t n = strlen(raw);
  if (n < 8 || n > PASS_MAX_LEN) {
    return false;
  }
  strlcpy(apPass, raw, sizeof(apPass));
  return true;
}

static void printApPassword(const char* elotag) {
  Serial.print(elotag);
  Serial.println(apPass);
}

void announceApPassword() {
  if (loadApPassword()) {
    printApPassword("AP beallito portal jelszava: ");
  }
}

// Betölti, vagy generál és ment egyet; csak bekapcsolt AP-mód után (az esp_random() így valódi véletlen).
static void ensureApPassword() {
  if (loadApPassword()) {
    return;
  }
  for (size_t i = 0; i < AP_PASS_LEN; i++) {
    apPass[i] = kApPassAlphabet[esp_random() & 31u];
  }
  apPass[AP_PASS_LEN] = '\0';
  printApPassword("UJ AP jelszo generalva (irdd fel az eszkozre!): ");

  char enc[SECRET_ENC_MAX + 1];
  bool mentve = false;
  if (filesystemReady() && encodeSecret(apPass, enc, sizeof(enc)) && beginConfigWrite()) {
    mentve = writeConfigValue(LittleFS, apPassPath, enc);
    endConfigWrite();
  }
  if (!mentve) {
    Serial.println("FIGYELEM: az AP jelszo mentese nem sikerult - csak erre a munkamenetre ervenyes.");
  }
}

// Az előkitöltött értékek escape-elése (az SSID tetszőleges bájt lehet).
static void printHtmlEscaped(AsyncResponseStream* r, const char* s) {
  for (const char* p = s; *p != '\0'; p++) {
    switch (*p) {
      case '&':  r->print(F("&amp;"));  break;
      case '<':  r->print(F("&lt;"));   break;
      case '>':  r->print(F("&gt;"));   break;
      case '"':  r->print(F("&quot;")); break;
      case '\'': r->print(F("&#39;"));  break;
      default:   r->write((uint8_t)*p); break;
    }
  }
}

// A jelszót soha nem töltjük elő.
static void sendConfigForm(AsyncWebServerRequest* request) {
  AsyncResponseStream* r = request->beginResponseStream("text/html", 2048);
  r->print(FORM_HEAD);

  r->print(F("SSID <input name=\"ssid\" maxlength=\"32\" required value=\""));
  printHtmlEscaped(r, ssid);
  r->print(F("\"><br>"));

  r->print(F("Jelszo <input name=\"pass\" type=\"password\" maxlength=\"63\">"
             " <small>(uresen hagyva a mentett jelszo marad)</small><br>"
             "<label><input type=\"checkbox\" name=\"open\" value=\"1\"> Nyilt halozat "
             "(jelszo nelkul)</label><br>"));

  r->print(F("IP cim <input name=\"ip\" maxlength=\"15\" placeholder=\"opcionalis\" value=\""));
  printHtmlEscaped(r, ipStr);
  r->print(F("\"><br>"));

  r->print(F("Atjaro (gateway) <input name=\"gateway\" maxlength=\"15\" placeholder=\"opcionalis\" value=\""));
  printHtmlEscaped(r, gatewayStr);
  r->print(F("\"><br>"));

  // A rejtett 0 akkor is elmegy, ha a jelolonegyzet ures; bejelolve az utana kuldott 1 gyoz.
  r->print(F("<label><input type=\"hidden\" name=\"outageap\" value=\"0\">"
             "<input type=\"checkbox\" name=\"outageap\" value=\"1\""));
  if (outageApEnabled) {
    r->print(F(" checked"));
  }
  r->print(F("> Wi-Fi-kieses alatt sajat halozat az allapotoldallal (192.168.4.1)</label><br>"));

  r->print(FORM_TAIL);
  request->send(r);
}

void touchApDeadline() {
  apDeadline = millis() + AP_TIMEOUT_MS;
}

static bool parseConfigPost(AsyncWebServerRequest* request, ConfigDraft& d,
                            const char*& failReason) {
  bool saveOk = true;
  failReason = nullptr;

  char candidate[PASS_MAX_LEN * 2 + 2];

  const size_t params = request->params();
  for (size_t i = 0; i < params; i++) {
    const AsyncWebParameter* p = request->getParam(i);
    if (!p->isPost()) {
      continue;
    }
    const String& name = p->name();
    const String& val = p->value();

    if (name == PARAM_SSID) {
      strlcpy(candidate, val.c_str(), sizeof(candidate));
      trimInPlace(candidate);

      if (val.length() < sizeof(candidate)
          && strlen(candidate) <= SSID_MAX_LEN && candidate[0] != '\0') {
        d.ssidProvided = true;
        strlcpy(d.ssid, candidate, sizeof(d.ssid));
      } else {
        Serial.println("Invalid SSID length!");
        saveOk = false;
        if (failReason == nullptr) failReason = "Ervenytelen SSID (1-32 karakter, nem csak szokoz).";
      }
    } else if (name == PARAM_PASS) {
      strlcpy(candidate, val.c_str(), sizeof(candidate));
      trimInPlace(candidate);

      if (val.length() < sizeof(candidate) && strlen(candidate) <= PASS_MAX_LEN) {
        strlcpy(d.pass, candidate, sizeof(d.pass));

        // A puffer elég voltát a fenti static_assert garantálja.
        d.passSet = encodeSecret(d.pass, d.passEnc, sizeof(d.passEnc));
      } else {
        Serial.println("Password too long!");
        saveOk = false;
        if (failReason == nullptr) failReason = "A jelszo tul hosszu (max 63 karakter).";
      }
    } else if (name == PARAM_IP) {
      strlcpy(candidate, val.c_str(), sizeof(candidate));
      trimInPlace(candidate);
      IPAddress testIP;
      if (candidate[0] == '\0') {
        d.ip[0] = '\0';
        d.ipSet = true;
      } else if (val.length() < sizeof(candidate)
                 && strlen(candidate) <= IPSTR_MAX_LEN && testIP.fromString(candidate)
                 && isUsableIPv4(testIP)) {
        strlcpy(d.ip, candidate, sizeof(d.ip));
        d.ipSet = true;
      } else {
        Serial.println("Invalid IP format!");
        saveOk = false;
        if (failReason == nullptr) failReason = "Ervenytelen IP cim (csak IPv4, nem 0.0.0.0).";
      }
    } else if (name == PARAM_OPEN) {
      if (val == "1") {
        d.openNet = true;
      } else {
        saveOk = false;
        if (failReason == nullptr) failReason = "Ervenytelen kapcsolo-ertek (nyilt halozat).";
      }
    } else if (name == PARAM_OUTAGE_AP) {
      if (val == "0" || val == "1") {
        d.outageApSet = true;
        d.outageAp = (val == "1");
      } else {
        saveOk = false;
        if (failReason == nullptr) failReason = "Ervenytelen kapcsolo-ertek (0 vagy 1).";
      }
    } else if (name == PARAM_GATEWAY) {
      strlcpy(candidate, val.c_str(), sizeof(candidate));
      trimInPlace(candidate);
      IPAddress testIP;
      if (candidate[0] == '\0') {
        d.gateway[0] = '\0';
        d.gwSet = true;
      } else if (val.length() < sizeof(candidate)
                 && strlen(candidate) <= IPSTR_MAX_LEN && testIP.fromString(candidate)
                 && isUsableIPv4(testIP)) {
        strlcpy(d.gateway, candidate, sizeof(d.gateway));
        d.gwSet = true;
      } else {
        Serial.println("Invalid gateway format!");
        saveOk = false;
        if (failReason == nullptr) failReason = "Ervenytelen gateway (csak IPv4, nem 0.0.0.0).";
      }
    }

    if (name == PARAM_PASS) {
      Serial.printf("POST[%s]: <%u chars>\n", name.c_str(), (unsigned)val.length());
    } else if (name == PARAM_SSID || name == PARAM_IP || name == PARAM_GATEWAY) {
      Serial.printf("POST[%s]: %.64s\n", name.c_str(), val.c_str());
    }
  }

  // Az ures jelszomezo a mentett jelszot hagyja meg; ures jelszot csak a kifejezett nyilt halozat ment.
  if (d.openNet) {
    if (d.passSet && d.pass[0] != '\0') {
      saveOk = false;
      if (failReason == nullptr) failReason = "Nyilt halozathoz hagyd uresen a jelszot.";
    } else {
      d.pass[0] = '\0';
      d.passSet = encodeSecret(d.pass, d.passEnc, sizeof(d.passEnc));
    }
  } else if (d.passSet && d.pass[0] == '\0') {
    d.passSet = false;
  }

  if (!d.ssidProvided) {
    saveOk = false;
    if (failReason == nullptr) failReason = "Hianyzo SSID.";
  }

  const char* ipFinal = d.ipSet ? d.ip : ipStr;
  const char* gwFinal = d.gwSet ? d.gateway : gatewayStr;
  if ((ipFinal[0] != '\0') != (gwFinal[0] != '\0')) {
    saveOk = false;
    if (failReason == nullptr) {
      failReason = "Statikus IP-hez az IP cimet ES a gateway-t is meg kell adni "
                   "(DHCP-hez hagyd mindkettot uresen).";
    }
  }

  if (ipFinal[0] != '\0' && gwFinal[0] != '\0') {
    IPAddress ipCim;
    IPAddress gwCim;
    if (!ipCim.fromString(ipFinal) || !gwCim.fromString(gwFinal)
        || !staticPairValid(ipCim, gwCim)) {
      saveOk = false;
      if (failReason == nullptr) {
        failReason = "Az IP cim es a gateway nem illik ossze: ugyanabban a "
                     "/24-es halozatban kell lenniuk (az elso harom szam "
                     "egyezzen), az utolso szam 1-254 legyen, es a ket cim "
                     "ne legyen azonos.";
      }
    }
  }

  return saveOk;
}

static bool commitConfigDraft(const ConfigDraft& d) {
  bool saveOk = true;
  strlcpy(ssid, d.ssid, sizeof(ssid));
  Serial.print("SSID set to: ");
  Serial.println(ssid);
  saveOk &= writeConfigValue(LittleFS, ssidPath, ssid);

  if (d.passSet) {
    strlcpy(pass, d.pass, sizeof(pass));
    Serial.print("Password set to: ");
    Serial.print((unsigned)strlen(pass));
    Serial.println(" chars");
    saveOk &= writeConfigValue(LittleFS, passPath, d.passEnc);
  }
  if (d.ipSet) {
    strlcpy(ipStr, d.ip, sizeof(ipStr));
    if (ipStr[0] == '\0') {
      Serial.println("IP empty, using DHCP.");
    } else {
      Serial.print("IP Address set to: ");
      Serial.println(ipStr);
    }
    saveOk &= writeConfigValue(LittleFS, ipPath, ipStr);
  }
  if (d.gwSet) {
    strlcpy(gatewayStr, d.gateway, sizeof(gatewayStr));
    if (gatewayStr[0] == '\0') {
      Serial.println("Gateway empty, using DHCP.");
    } else {
      Serial.print("Gateway set to: ");
      Serial.println(gatewayStr);
    }
    saveOk &= writeConfigValue(LittleFS, gatewayPath, gatewayStr);
  }
  if (d.outageApSet) {
    outageApEnabled = d.outageAp;
    Serial.print("Kieses alatti AP: ");
    Serial.println(outageApEnabled ? "be" : "ki");
    saveOk &= writeConfigValue(LittleFS, outageApPath, outageApEnabled ? "1" : "0");
  }
  return saveOk;
}

// Validálás, majd a konfigzár alatt írás és halasztott újraindítás.
static void handleConfigPost(AsyncWebServerRequest* request) {
  touchApDeadline();
  if (!filesystemReady()) {
    request->send(500, "text/plain",
                  "LittleFS nem elerheto, a beallitasok nem menthetok. "
                  "Ellenorizd a particios semat (partitions_custom.csv, "
                  "'spiffs' cimkeju particio).");
    return;
  }

  ConfigDraft draft;
  const char* failReason = nullptr;
  if (!parseConfigPost(request, draft, failReason)) {
    touchApDeadline();
    Serial.println("A beallitasok mentese SIKERTELEN.");

    char err[208];
    snprintf(err, sizeof(err),
               "A beallitasok mentese nem sikerult: %s "
               "Az eszkoz NEM indul ujra, probald meg ismet.",
               failReason ? failReason : "ismeretlen hiba.");
    request->send(500, "text/plain", err);
    return;
  }

  if (!beginConfigWrite()) {
    request->send(503, "text/plain",
                  "Az eszkoz eppen a konfiguraciot irja (gombos torles vagy "
                  "masik mentes). Probald ujra egy pillanat mulva.");
    return;
  }

  const bool saveOk = commitConfigDraft(draft);
  endConfigWrite();
  touchApDeadline();

  if (!saveOk) {
    Serial.println("A beallitasok mentese SIKERTELEN.");
    request->send(500, "text/plain",
                    "A beallitasok mentese nem sikerult: LittleFS irasi hiba. "
                    "Az eszkoz NEM indul ujra, probald meg ismet.");
    return;
  }

  char message[96];
  if (ipStr[0] != '\0') {
    snprintf(message, sizeof(message),
             "Mentve. Az ESP ujraindul, utana a %s cimen erheto el.", ipStr);
  } else {
    snprintf(message, sizeof(message),
             "Mentve. Az ESP ujraindul, es DHCP-vel csatlakozik a routerhez.");
  }
  request->send(200, "text/plain", message);

  logEvent(EV_CONFIG_SAVED, 0);
  requestRestart(RESTART_GRACE_MS);
}

static void printLogRow(AsyncResponseStream* r, const EventEntry& e,
                        const char* forras) {
  char mikor[24];
  r->print(F("<tr><td>"));
  if (formatEpoch(e.epoch, mikor, sizeof(mikor))) {
    r->print(mikor);
  } else {
    r->print(F("-"));
  }
  r->printf("</td><td>%u:%02u:%02u</td><td>%s</td><td>%u</td><td>%s</td></tr>",
            (unsigned)(e.uptimeSec / 3600), (unsigned)((e.uptimeSec % 3600) / 60),
            (unsigned)(e.uptimeSec % 60), eventName(e.code), (unsigned)e.param,
            forras);
}

// A /log oldal: állapot, napló (fájl + RTC), jelmagyarázat.
static void sendDiagnosticLog(AsyncWebServerRequest* request) {
  touchApDeadline();

  uint32_t evTotal, mentettEddig;
  EventEntry evCopy[EVLOG_SIZE];
  portENTER_CRITICAL(&evLogMux);
  evTotal = (rtcEvMagic == EVLOG_MAGIC) ? rtcEvNext : 0;
  mentettEddig = rtcSavedEvNext;
  memcpy(evCopy, rtcEvents, sizeof(evCopy));
  portEXIT_CRITICAL(&evLogMux);

  EvFileHeader fej;
  const bool vanFajl = !configWriteInProgress() && loadEventLogHeader(fej);
  const uint16_t fajlDb = vanFajl ? fej.count : 0;

  const uint32_t alap = vanFajl
      ? (mentettEddig < fej.evNextAtSave ? mentettEddig : fej.evNextAtSave) : 0;
  const uint32_t keletkezett = (evTotal > alap) ? (evTotal - alap) : 0;
  const uint16_t ujDb = (uint16_t)(keletkezett < EVLOG_SIZE ? keletkezett : EVLOG_SIZE);

  const size_t sorok = (size_t)fajlDb + (size_t)ujDb;
  size_t pufferMeret = LOG_PAGE_FIX_BYTES + sorok * LOG_PAGE_ROW_BYTES;
  if (pufferMeret < LOG_STREAM_MIN) {
    pufferMeret = LOG_STREAM_MIN;
  }
  AsyncResponseStream* r = request->beginResponseStream("text/html", pufferMeret);
  r->print(F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
             "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
             "<title>Naplo</title></head><body><h2>Diagnosztikai naplo</h2>"));

  const esp_reset_reason_t rr = esp_reset_reason();
  r->printf("<p><b>Utolso indulas oka:</b> %s (%d)<br>",
            resetReasonName(rr), (int)rr);

  const DiagCounters dc = diagCounters();
  r->printf("<b>Watchdog ujraindulasok:</b> %u / %u<br>",
            (unsigned)dc.wdtResets, (unsigned)dc.wdtLimit);
  r->printf("<b>Wi-Fi lepcso:</b> %u. fok, kieses %u perce<br>",
            (unsigned)dc.wifiStep, (unsigned)dc.wifiOutageMin);

  {
    const uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
    r->printf("<b>Uptime:</b> %ud %uh %um %us</p>",
              (unsigned)(up / 86400), (unsigned)((up % 86400) / 3600),
              (unsigned)((up % 3600) / 60), (unsigned)(up % 60));
  }

  if (fajlDb == 0 && ujDb == 0) {
    r->print(F("<p>Nincs rogzitett esemeny.</p>"));
  } else {
    r->print(F("<p><b>Forras:</b> "));
    if (vanFajl) {
      char mikor[24];
      r->printf("a fajlrendszerre mentett naplo mind a %u bejegyzese", (unsigned)fajlDb);
      if (formatEpoch(fej.savedEpoch, mikor, sizeof(mikor))) {
        r->printf(" (utoljara mentve: %s)", mikor);
      } else {
        r->printf(" (utoljara mentve a bootolas utan %u:%02u:%02u-kor)",
                  (unsigned)(fej.savedUptime / 3600),
                  (unsigned)((fej.savedUptime % 3600) / 60),
                  (unsigned)(fej.savedUptime % 60));
      }
      r->print(F(", plusz "));
    } else {
      r->print(F("nincs olvashato naplofajl, csak "));
    }
    r->printf("az RTC memoriabol %u, a mentes ota keletkezett bejegyzes.</p>",
              (unsigned)ujDb);

    r->print(F("<table border=1 cellpadding=4><tr><th>Ido</th><th>Uptime</th>"
               "<th>Esemeny</th><th>Param</th><th>Forras</th></tr>"));

    if (fajlDb > 0) {
      EventEntry darab[8];
      uint32_t tol = 0;
      while (tol < fajlDb) {
        const uint16_t hatra = (uint16_t)(fajlDb - tol);
        const uint16_t db = (uint16_t)(hatra < 8 ? hatra : 8);
        if (!loadEventLogRange(fej, tol, db, darab)) {
          r->print(F("<tr><td colspan=5>a naplofajl olvasasa megszakadt</td></tr>"));
          break;
        }
        for (uint16_t i = 0; i < db; i++) {
          printLogRow(r, darab[i], "fajl");
        }
        tol += db;
      }
    }

    for (uint16_t i = 0; i < ujDb; i++) {
      printLogRow(r, evCopy[(evTotal - ujDb + i) % EVLOG_SIZE], "RTC");
    }
    r->print(F("</table>"));
  }

  r->print(F("<h3>Param jelentese</h3><ul>"
             "<li>BOOT: az indulas oka (lasd fent)</li>"
             "<li>WIFI OK: a Wi-Fi lepcso foka csatlakozaskor (0 = nem volt kieses, "
             "1 = az ESP ujrainditasa segitett, 2 vagy tobb = N-1 router reset utan)</li>"
             "<li>WIFI LOST: a WiFi.status() erteke</li>"
             "<li>TEST FAIL: kieses kezdete; a bukott tartalek HTTP teszt sorszama (1-5)</li>"
             "<li>ROUTER RESET: hanyadik ujrainditas ebben a kiesesben</li>"
             "<li>AP MODE: 1 = nincs SSID, 2 = rossz jelszo, "
             "3 = 48 oraja nincs Wi-Fi, 4 = a gateway sem erheto el, "
             "5 = mentes utan nem sikerult csatlakozni, a beallitasok torolve</li>"
             "<li>CONFIG SAVED: mindig 0 - itt maga a bejegyzes az "
             "informacio, parametere nincs</li>"
             "<li>GW UNREACH: 1 = a router reset elott, 2 = utana is</li>"
             "<li>SLEEP: 3 = AP idotullepes, 4 = vegzetes hiba</li>"
             "<li>WIFI RESTART: az ESP a Wi-Fi miatt indult ujra (param: a WiFi.status() erteke)</li>"
             "<li>FATAL: 1 = LittleFS, 2 = konfig olvasas, "
             "3 = watchdog, 4 = wifireset torles, 5 = tartosan keves heap</li>"
             "<li>WDT RESET: hanyadik rendellenes ujraindulas</li>"
             "<li>STUCK BUTTON: 0 = reset gomb, 1 = wifireset gomb</li>"
             "<li>LOW HEAP: a szabad heap KB-ban a kuszob atlepesekor</li>"
             "<li>HEAP RESTART: a szabad heap KB-ban az onkentes "
             "ujraindulas elott</li>"
             "<li>HTTP ERROR: a tartalek HTTP teszt kodja. 200/204/403/5xx: a "
             "szerver valasza (200 = jott valasz, de nem a vart szoveg). "
             "1000 felett kliens hiba, 1000 + a kod: 1001 = nem sikerult "
             "kapcsolodni vagy a nevet feloldani, 1011 = nem jott valasz "
             "idoben, 1100 = a kerest nem lehetett inditani</li>"
             "<li>NET DIAG: a router reset oka. 1 = a router sem valaszol "
             "(vagy nincs Wi-Fi), 2 = a router valaszol, az internet nem, "
             "3 = az internet el, de a router DNS-e 10 perce hallgat</li>"
             "</ul>"));
  r->print(F("<p><i>Az uptime minden indulaskor nullarol indul, ezert a "
             "BOOT sorok jelzik az ujraindulasokat. A naplo az "
             "aramtalanitast nem eli tul.</i></p>"
             "<p><a href=\"/\">Vissza a beallitasokhoz</a></p>"));

  r->print(KEEPALIVE_JS);
  r->print(F("</body></html>"));
  request->send(r);
}

static bool outageAp = false;

// Csak a saját alhálózatról (a router mögül) vagy a kiesés alatti saját hálózatunkról szolgálunk ki, máshonnan 403.
static bool fromLocalNet(AsyncWebServerRequest* request) {
  const uint32_t tav = (uint32_t)request->client()->remoteIP();
  const uint32_t sajat = (uint32_t)WiFi.localIP();
  const uint32_t maszk = (uint32_t)WiFi.subnetMask();
  if (sajat != 0 && maszk != 0 && (tav & maszk) == (sajat & maszk)) {
    return true;
  }
  const uint32_t apCim = (uint32_t)WiFi.softAPIP();
  const uint32_t apMaszk = (uint32_t)WiFi.softAPSubnetMask();
  return outageAp && apCim != 0 && apMaszk != 0 && (tav & apMaszk) == (apCim & apMaszk);
}

static void printYesNoMin(AsyncResponseStream* r, const char* mi, bool aktiv,
                          uint32_t perc, uint32_t resetek, const char* rendben) {
  r->printf("<tr><td>%s</td><td>", mi);
  if (aktiv) {
    r->printf("<b>hiba, %u perce</b>", (unsigned)perc);
  } else {
    r->print(rendben);
  }
  r->printf("</td><td>%u</td></tr>", (unsigned)resetek);
}

// Az állapotoldal: a számlálók és az utolsó események az RTC naplóból (fájlt nem olvas).
static void sendStatusPage(AsyncWebServerRequest* request) {
  if (!fromLocalNet(request)) {
    request->send(403, "text/plain", "Csak a helyi halozatrol");
    return;
  }
  uint32_t evTotal;
  EventEntry evCopy[EVLOG_SIZE];
  portENTER_CRITICAL(&evLogMux);
  evTotal = (rtcEvMagic == EVLOG_MAGIC) ? rtcEvNext : 0;
  memcpy(evCopy, rtcEvents, sizeof(evCopy));
  portEXIT_CRITICAL(&evLogMux);
  const uint32_t elerheto = evTotal < EVLOG_SIZE ? evTotal : EVLOG_SIZE;
  const uint16_t db = (uint16_t)(elerheto < STATUS_EVENTS ? elerheto : STATUS_EVENTS);

  const DiagCounters dc = diagCounters();
  AsyncResponseStream* r = request->beginResponseStream("text/html", LOG_STREAM_MIN);
  r->print(F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
             "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
             "<title>Allapot</title></head><body><h2>Router figyelo - allapot</h2>"));
  const uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
  const esp_reset_reason_t rr = esp_reset_reason();
  r->printf("<p><b>Uptime:</b> %ud %uh %um<br><b>Utolso indulas oka:</b> %s (%d)<br>"
            "<b>Szabad heap:</b> %u B</p>",
            (unsigned)(up / 86400), (unsigned)((up % 86400) / 3600), (unsigned)((up % 3600) / 60),
            resetReasonName(rr), (int)rr, (unsigned)ESP.getFreeHeap());
  r->print(F("<table border=1 cellpadding=4><tr><th>Mit</th><th>Allapot</th>"
             "<th>Resetek ebben a hibaban</th></tr>"));
  printYesNoMin(r, "Internet", dc.netOutage, dc.netOutageMin, dc.netOutage ? dc.netResets : 0, "rendben");
  printYesNoMin(r, "A router DNS-e", dc.dnsSuspect, dc.dnsSuspectMin, dc.dnsResets, "rendben");
  // Kieses kozben az oldal csak a sajat halozatrol erheto el; utana meg 30 percig jelzi a lezajlott kiesest.
  r->print(F("<tr><td>Wi-Fi</td><td>"));
  if (!dc.wifiUp) {
    if (dc.wifiStep > 0) {
      r->printf("<b>nincs kapcsolat, %u perce</b>", (unsigned)dc.wifiOutageMin);
    } else {
      r->print(F("<b>nincs kapcsolat</b>"));
    }
  } else if (dc.wifiStep > 0) {
    r->print(F("rendben (nemreg kiesett, a lepcso 30 perc stabil kapcsolat utan nullazodik)"));
  } else {
    r->print(F("rendben"));
  }
  r->print(F("</td><td>-</td></tr>"));
  r->print(F("</table>"));
  r->printf("<p><b>Router reset most:</b> %s<br><b>Watchdog ujraindulasok:</b> %u / %u<br>"
            "<b>Heap miatti ujraindulasok:</b> %u</p>",
            dc.resetRunning ? "folyamatban" : "nem", (unsigned)dc.wdtResets,
            (unsigned)dc.wdtLimit, (unsigned)dc.heapRestarts);
  r->printf("<h3>Utolso %u esemeny</h3>", (unsigned)db);
  if (db == 0) {
    r->print(F("<p>Nincs rogzitett esemeny.</p>"));
  } else {
    r->print(F("<table border=1 cellpadding=4><tr><th>Ido</th><th>Uptime</th>"
               "<th>Esemeny</th><th>Param</th><th>Forras</th></tr>"));
    for (uint16_t i = 0; i < db; i++) {
      printLogRow(r, evCopy[(evTotal - db + i) % EVLOG_SIZE], "RTC");
    }
    r->print(F("</table>"));
  }
  r->print(F("<p><i>Csak olvashato oldal. A teljes naplo es a beallitasok "
             "az AP beallito modban erhetok el.</i></p></body></html>"));
  request->send(r);
}

void startStatusPage() {
  if (statusRunning) {
    return;
  }
  if (!statusHandlersSet) {
    statusServer.on("/", HTTP_GET, [](AsyncWebServerRequest* request) { sendStatusPage(request); });
    statusServer.onNotFound([](AsyncWebServerRequest* request) {
      request->send(404, "text/plain", "Nem talalhato");
    });
    statusHandlersSet = true;
  }
  statusServer.begin();
  statusRunning = true;
  Serial.println("Allapotoldal elinditva (csak olvashato, csak a helyi halozatrol).");
}

void stopStatusPage() {
  if (!statusRunning) {
    return;
  }
  statusServer.end();
  statusRunning = false;
}

static void makeApName(char* out, size_t outSize) {
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(out, outSize, "ESP-%s-%02X%02X", ESP.getChipModel(),
           (unsigned)((mac >> 32) & 0xFF), (unsigned)((mac >> 40) & 0xFF));
}

bool outageApActive() {
  return outageAp;
}

// A router keresése mellett (AP+STA) saját hálózat; csak az állapotoldal érhető el rajta.
void startOutageAp() {
  if (outageAp) {
    return;
  }
  char apName[32];
  makeApName(apName, sizeof(apName));
  WiFi.mode(WIFI_AP_STA);
  ensureApPassword();
  if (!WiFi.softAP(apName, apPass)) {
    Serial.println("A kieses alatti AP nem indult el.");
    WiFi.mode(WIFI_STA);
    return;
  }
  outageAp = true;
  Serial.print("Kieses alatti AP: ");
  Serial.print(apName);
  Serial.print(", allapotoldal: http://");
  Serial.println(WiFi.softAPIP());
}

void stopOutageAp() {
  if (!outageAp) {
    return;
  }
  WiFi.softAPdisconnect(true);
  outageAp = false;
  Serial.println("A Wi-Fi visszajott - a kieses alatti AP leallt.");
}

// A LittleFS-ről semmit nem szolgálunk ki (a /pass.txt-t is kiadná).
void startWebPortal() {
  Serial.println("Setting AP (Access Point)");

  char apName[32];
  makeApName(apName, sizeof(apName));
  outageAp = false;
  WiFi.mode(WIFI_AP);
  ensureApPassword();
  WiFi.softAP(apName, apPass);
  Serial.print("AP SSID: ");
  Serial.println(apName);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    touchApDeadline();
    sendConfigForm(request);
  });

  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest* request) {
    touchApDeadline();
    request->send(200, "text/plain", "1");
  });

  server.on("/log", HTTP_GET, [](AsyncWebServerRequest* request) {
    touchApDeadline();
    sendDiagnosticLog(request);
  });

  server.onNotFound([](AsyncWebServerRequest* request) {
    touchApDeadline();
    request->send(404, "text/plain", "Nem talalhato");
  });

  server.on("/", HTTP_POST, [](AsyncWebServerRequest* request) {
    handleConfigPost(request);
  });

  server.begin();
}

void stopWebPortal() {
  server.end();
}
