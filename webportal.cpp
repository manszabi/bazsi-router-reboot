#include "webportal.h"

// Az AsyncTCP-t az ESPAsyncWebServer amugy is behuzza; itt EXPLICITEN is
// szerepel, mert az Arduino a konyvtarakat az includeok alapjan deriti fel, es
// ezt a lancot nem akarjuk egy kozvetett fuggosegre bizni.
#include <AsyncTCP.h>
#include <WiFi.h>
#include <string.h>

#include "app_hooks.h"
#include "configstore.h"
#include "eventlog.h"
#include "limits_config.h"
#include "secret.h"
#include "strutil.h"
#include "sync.h"

// A valasz kikuldese es az ujraindulas kozotti turelmi ido. A halasztast a
// loop task hajtja vegre (sync.h) - itt csak kerjuk.
static constexpr uint32_t RESTART_GRACE_MS = 2000;

// ---------------------------------------------------------------------------
// A POST kezelo ket fazisa es a kozottuk atmeno adat.
//
// MIERT KULON FUGGVENYBEN? Ez a ket kezelo korabban a startConfigPortal()
// belsejeben, a server.on() argumentumaban allo lambdakent volt megirva -
// egyutt 470 sor egyetlen fuggvenyben. A POST kezelo ezen belul is
// kulonleges: ezen megy be a Wi-Fi jelszo, tehat ez a program
// biztonsagkritikus felulete. Egy 290 soros lambda egy fuggvenyhivas
// argumentumaban nem nezheto at tisztessegesen, es a ket fazisa (validalas /
// commit) sem valik el a tipusokban - pedig epp az elvalasztasukon mulik,
// hogy egy hibas mezo ne hagyjon fel-uj, fel-regi konfiguraciot a flashben.
// ---------------------------------------------------------------------------

// A beerkezo urlap mezoi, VALIDALVA, de meg NEM ervenyesitve. Ez az egyetlen
// adat, ami az 1. es a 2. fazis kozott atmegy. Amig csak ide irunk, sem fajlt
// nem irtunk, sem a futo konfiguraciot nem valtoztattuk - ezt a garanciat
// most mar a TIPUS is hordozza, nem csak a komment.
//
// A "Set" jelzok azt mondjak meg, mely mezok erkeztek a keresben; ami nem
// erkezett, annak a mentett erteke marad ervenyben.
//
// VEREM: a struktura tagjai pontosan azok a pufferek, amik korabban kulon-
// kulon a lambda vermen alltak (33+64+130+16+16 bajt) - a meret tehat nem
// nott, csak egy helyre kerult. Ez szamit: az async_tcp task verme veges.
struct ConfigDraft {
  char ssid[SSID_MAX_LEN + 1]      = { 0 };
  char pass[PASS_MAX_LEN + 1]      = { 0 };
  char passEnc[SECRET_ENC_MAX + 1] = { 0 };

// A KODOLASI AG PREMISSZAJA, FORDITASIDOBEN. A parseConfigPost()-ban van egy
// ag arra az esetre, ha a kodolt jelszo nem ferne a pufferbe - es az a
// lefedettsegben SZANDEKOSAN feher marad, mert szerkezetileg elerhetetlen.
// "Szerkezetileg elerhetetlen" viszont csak addig igaz, amig ez a ket meret
// egyutt mozog. Eddig ezt csak a komment allitotta; mostantol a FORDITO.
static_assert(sizeof(ConfigDraft::pass) - 1 <= PASS_MAX_LEN,
              "a ConfigDraft jelszo-mezoje nem lehet hosszabb PASS_MAX_LEN-nel");
static_assert(SECRET_ENC_MAX >= SECRET_PREFIX_LEN + 2 * (sizeof(ConfigDraft::pass) - 1),
              "a kodolt jelszo pufferenek MINDIG elegnek kell lennie - kulonben "
              "a parseConfigPost() 'nem fert a pufferbe' aga elerhetove valna");
  char ip[IPSTR_MAX_LEN + 1]       = { 0 };
  char gateway[IPSTR_MAX_LEN + 1]  = { 0 };
  bool ssidProvided = false;
  bool passSet      = false;
  bool ipSet        = false;
  bool gwSet        = false;
};

// A webszerver. STATIC: a modulon kivul senki nem nyulhat hozza - az
// inditasa es a leallitasa a ket publikus fuggvenyen at megy.
static AsyncWebServer server(80);

// Search for parameter in HTTP POST request
const char PARAM_SSID[]    = "ssid";
const char PARAM_PASS[]    = "pass";
const char PARAM_IP[]      = "ip";
const char PARAM_GATEWAY[] = "gateway";

// Keep-alive: amíg a lap NYITVA van, 60 mp-enként jelez. Enélkül az AP mód
// "tétlenség" órája a lap betöltésétől ketyegne, nem az utolsó interakciótól -
// és egy lassan begépelt jelszó közben elaludna az eszköz (mérve: 6 perc
// gépelés után a Submit már nem érné el).
// A visibilitychange azért kell, hogy app-váltás után visszatérve azonnal
// frissüljön a határidő, ne csak a következő 60 mp-es ütemnél.
// Mindkét űrlapon és a naplóoldalon ugyanez a szöveg szerepel; a const char[]
// a flash .rodata szekciójába kerül (drom0_0_seg), RAM-ot nem foglal.
// Egyetlen forrás: a fordító fűzi össze a literálokat, futásidőben nem másolunk.
#define KEEPALIVE_JS_LIT \
  "<script>f=()=>fetch('/ping?'+Date.now());setInterval(f,6e4);" \
  "document.onvisibilitychange=()=>document.hidden||f()</script>"
const char KEEPALIVE_JS[] = KEEPALIVE_JS_LIT;

// A beállító űrlap FEJLÉCE és LÁBLÉCE. A középső, mezőket tartalmazó rész
// futásidőben generálódik, mert a mentett SSID-t, IP-t és gateway-t
// ELŐKITÖLTVE mutatjuk - lásd sendConfigForm().
const char FORM_HEAD[] =
  "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
  "<title>ESP Wi-Fi Manager</title></head><body>"
  "<h2>ESP Wi-Fi Manager</h2>"
  "<form action=\"/\" method=\"POST\">";
const char FORM_TAIL[] =
  "<small>Statikus IP-hez mindket cimmezot toltsd ki, csak IPv4. "
  "DHCP-hez hagyd mindkettot uresen.</small><br>"
  "<input type=\"submit\" value=\"Submit\"></form>"
  "<p><a href=\"/log\">Diagnosztikai naplo</a></p>"
  KEEPALIVE_JS_LIT
  "</body></html>";

// AP password (WPA2: min. 8, max. 63 karakter)
const char AP_PASSWORD[] = "12345678";
// A WiFi.softAP() visszateresi erteket nem nezzuk, a core pedig rovid jelszonal
// csak annyit tesz, hogy "passphrase too short!" es return false (AP.cpp) - az
// AP letre sem jonne, az eszkoz elerhetetlen lenne, es 5 perc mulva elaludna.
// Ezt fordítási idoben fogjuk meg, hogy egy kesobbi atiras ne tudja elrontani.
static_assert(sizeof(AP_PASSWORD) - 1 >= 8, "AP jelszo: legalabb 8 karakter (WPA2)");
static_assert(sizeof(AP_PASSWORD) - 1 <= 63, "AP jelszo: legfeljebb 63 karakter (WPA2)");

// HTML-escape egy attribútumérték számára.
//
// MIÉRT KELL: az SSID tetszőleges 32 bájt lehet - idézőjelet, `<`-t és `&`-et
// is tartalmazhat. Escape NÉLKÜL az előkitöltés maga nyitna biztonsági rést a
// saját portálunkon: egy idézőjel kitörne a value="..." attribútumból, egy
// <script> pedig a lapba kerülne. A mentett SSID ráadásul a POST kezelőn át
// bármi lehet, tehát ez nem elméleti.
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

// A beállító űrlap kiszolgálása, a mentett értékekkel ELŐKITÖLTVE.
//
// MIÉRT ELŐKITÖLTVE? Mert az üres címmező TÖRLÉST jelent. Aki statikus IP-vel
// üzemel és csak a jelszót akarja átírni, annak a böngésző üresen küldené a
// cím mezőket - és a mentés csendben DHCP-re váltana. (Mérve: AP1.)
//
// A JELSZÓT SOHA NEM töltjük elő: az az egyetlen titok ezen a lapon, és a
// portál WPA2 kulcsa nyilvános, tehát a lap tartalma nem tekinthető védettnek.
static void sendConfigForm(AsyncWebServerRequest* request) {
  // A KEZDO PUFFERMERET MERT ERTEK, nem becsles (AP8 forgatokonyv):
  //   tipikus eset ("MyHomeNetwork" + ket valodi IP):     903 bajt
  //   legrosszabb eset (32+15+15 karakter, mind escape-elve): 1238 bajt
  // Az utobbi nem elmeleti: az SSID barmi lehet, es a printHtmlEscaped() egy
  // idezojelbol HAT bajtot csinal (&quot;); a cimmezoket pedig egy serult
  // /ip.txt is feltoltheti tetszoleges karakterekkel.
  //
  // A korabbi 1024 MINDKET esetnel szuk volt (a tipikusnal is csak 121 bajt
  // tartalek), tehat a stream ujraallokalt volna - epp az async_tcp taskban,
  // ahol a heap a legszukebb, es epp azt kerulnenk el a kezdomerettel.
  // 1536 a mert maximum folott ~24% tartalekot ad.
  AsyncResponseStream* r = request->beginResponseStream("text/html", 1536);
  r->print(FORM_HEAD);

  r->print(F("SSID <input name=\"ssid\" maxlength=\"32\" required value=\""));
  printHtmlEscaped(r, ssid);
  r->print(F("\"><br>"));

  // A jelszó mező ÜRESEN indul, és a következménye ki van írva: enélkül a
  // felhasználó azt hinné, hogy a mentett jelszó megmarad.
  r->print(F("Password <input name=\"pass\" type=\"password\" maxlength=\"63\">"
             " <small>(ures = nyilt halozat)</small><br>"));

  r->print(F("IP <input name=\"ip\" maxlength=\"15\" placeholder=\"opcionalis\" value=\""));
  printHtmlEscaped(r, ipStr);
  r->print(F("\"><br>"));

  r->print(F("Gateway <input name=\"gateway\" maxlength=\"15\" placeholder=\"opcionalis\" value=\""));
  printHtmlEscaped(r, gatewayStr);
  r->print(F("\"><br>"));

  r->print(FORM_TAIL);
  request->send(r);
}

// Az AP beállító mód visszaszámlálásának újraindítása. Minden HTTP kérésnél
// meghívjuk, így ha a felhasználó az utolsó pillanatban nyitja meg az oldalt,
// kap még egy teljes időablakot a kitöltésre.
void touchApDeadline() {
  apDeadline = millis() + AP_TIMEOUT_MS;
}

// 1. FAZIS: a bejovo mezok beolvasasa es validalasa a draft-be.
// Igaz, ha MINDEN mezo ervenyes. Hamis eseten a failReason az ELSO hiba oka.
// Ez a fuggveny sem fajlt nem ir, sem globalist nem modosit.
static bool parseConfigPost(AsyncWebServerRequest* request, ConfigDraft& d,
                            const char*& failReason) {
  bool saveOk = true;
  failReason = nullptr;

  // KOZOS jelolt-puffer mind a negy mezohoz, a legnagyobbhoz (jelszo)
  // meretezve, es a mezo hatarnal BOVEBBRE: igy a masolas-beillesztes
  // szokozeit le tudjuk vagni MIELOTT a hosszat merjuk. Egy puffer, mert az
  // async_tcp task verme veges, es igy a negy ag nem rakodik egymasra.
  char candidate[PASS_MAX_LEN * 2 + 2];

  // size_t, nem int: a valodi AsyncWebServerRequest::params() ezt adja
  // vissza, es a szukites -Wconversion mellett figyelmeztetest is kap.
  const size_t params = request->params();
  for (size_t i = 0; i < params; i++) {
    const AsyncWebParameter* p = request->getParam(i);
    if (!p->isPost()) {
      continue;
    }
    const String& name = p->name();
    const String& val = p->value();  // referencia: nincs felesleges másolat

    if (name == PARAM_SSID) {
      // A readConfigValue() beolvasáskor levágja a whitespace-t, ezért már
      // itt is levágjuk: így az elmentett és a visszajelzett érték pontosan
      // az, amivel az eszköz később csatlakozni fog. A csupa szóközből álló
      // SSID így üresre fogy - azt pedig nem szabad sikerként elfogadni,
      // mert az újraindulás után AP módban kötnénk ki.
      strlcpy(candidate, val.c_str(), sizeof(candidate));
      trimInPlace(candidate);
      // A hosszat a VAGAS UTAN merjuk - ugyanugy, mint az IP/gateway agon.
      // Korabban a nyers val.length() dontott, ezert egy 32 karakteres SSID
      // egyetlen beillesztett zaro szokozzel (33 bajt) "tul hosszu"-kent
      // bukott el, holott a vagott ertek tokeletesen ervenyes. A
      // val.length() feltetel megmarad, de mar csak a CSONKOLAS ellen: egy
      // a pufferbe sem fero bemenet vege csendben elveszne.
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
      // A hosszat itt is a VAGAS UTAN merjuk (lasd az SSID agat): egy 63
      // karakteres jelszo egyetlen beillesztett zaro szokozzel korabban
      // "tul hosszu" hibat adott, pedig a vagas amugy is megtortent volna.
      if (val.length() < sizeof(candidate) && strlen(candidate) <= PASS_MAX_LEN) {
        strlcpy(d.pass, candidate, sizeof(d.pass));
        // TUDATOS KORLAT: a vagas a masolas-beillesztessel bekerult szokozok
        // ellen szol, es a WPA2 szabvany szerint egy jelszo ELEJEN vagy VEGEN
        // allo szokoz onmagaban ervenyes volna. Ilyen jelszo ezzel az
        // eszkozzel NEM allithato be - a mentett ertek a vagott valtozat lesz.
        // Nem a tarolas kenyszeriti ki: a jelszo "v1:" + hexa alakban megy a
        // fajlba, abban nincs szokoz, tehat a kodolas/dekodolas a szokozt
        // hibatlanul visszaadna (ellentetben az SSID-vel, ami sima szovegkent
        // tarolodik, es amit a readConfigValue() beolvasaskor ugyis vag).
        // (A vagas mar a candidate pufferben megtortent.)
        // Összekeverve mentjük, hogy egy flash dumpon a `strings` ne adjon
        // használható jelszót. A visszaolvasásos ellenőrzés érintetlen: a
        // writeConfigValue() a kódolt formát verifikálja.
        if (encodeSecret(d.pass, d.passEnc, sizeof(d.passEnc))) {
          d.passSet = true;
        } else {
          // Szerkezetileg elerhetetlen ag: a d.pass legfeljebb PASS_MAX_LEN
          // (63) karakter, a kodolt alak pedig "v1:" + 2 hexa karakter
          // bajtonkent = 3 + 126 = 129, a puffer viszont pontosan ekkora
          // (SECRET_ENC_MAX). Vedelmi halo arra az esetre, ha valaki a ket
          // konstans kozul csak az egyiket modositana - ezert a
          // lefedettsegben feher marad.
          Serial.println("- a jelszo kodolasa nem fert a pufferbe");
          saveOk = false;
          if (failReason == nullptr) failReason = "Belso hiba a jelszo mentesekor.";
        }
      } else {
        Serial.println("Password too long!");
        saveOk = false;
        if (failReason == nullptr) failReason = "A jelszo tul hosszu (max 63 karakter).";
      }
    } else if (name == PARAM_IP) {
      // Az SSID-hez es a jelszohoz hasonloan a masolas-beillesztes szokozeit
      // itt is levagjuk a validalas elott: a "192.168.1.200 " szandeka
      // egyertelmu. A puffer bovebb a mezonel, hogy a szokozokkel egyutt is
      // beferjen a vagas elott; a vagott ertek hosszat kulon ellenorizzuk.
      // A csupa szokoz uresre fogy = DHCP, nem hibauzenet.
      strlcpy(candidate, val.c_str(), sizeof(candidate));
      trimInPlace(candidate);
      IPAddress testIP;
      if (candidate[0] == '\0') {
        d.ip[0] = '\0';
        d.ipSet = true;
      } else if (val.length() < sizeof(candidate)
                 && strlen(candidate) <= IPSTR_MAX_LEN && testIP.fromString(candidate)
                 && isUsableIPv4(testIP)) {
        // A val.length() feltetel az SSID/jelszo agak explicit hossz-
        // ellenorzesenek parja: a strlcpy() csonkolasa utan egy tulmeretes
        // bemenet vege csendben elveszne, es a maradek akar ervenyes cimme
        // is vagodhatna - ilyet nem fogadunk el, az a hiba-agra tartozik.
        strlcpy(d.ip, candidate, sizeof(d.ip));
        d.ipSet = true;
      } else {
        Serial.println("Invalid IP format!");
        saveOk = false;
        if (failReason == nullptr) failReason = "Ervenytelen IP cim (csak IPv4, nem 0.0.0.0).";
      }
    } else if (name == PARAM_GATEWAY) {
      // Ugyanaz a vagas, mint az IP-nel.
      strlcpy(candidate, val.c_str(), sizeof(candidate));
      trimInPlace(candidate);
      IPAddress testIP;
      if (candidate[0] == '\0') {
        d.gateway[0] = '\0';
        d.gwSet = true;
      } else if (val.length() < sizeof(candidate)
                 && strlen(candidate) <= IPSTR_MAX_LEN && testIP.fromString(candidate)
                 && isUsableIPv4(testIP)) {
        // Lasd az IP ag megjegyzeset a csonkolas elleni vedelemrol.
        strlcpy(d.gateway, candidate, sizeof(d.gateway));
        d.gwSet = true;
      } else {
        Serial.println("Invalid gateway format!");
        saveOk = false;
        if (failReason == nullptr) failReason = "Ervenytelen gateway (csak IPv4, nem 0.0.0.0).";
      }
    }

    // CSAK A NEGY ISMERT MEZOT visszhangozzuk, es a hosszat is korlatozzuk.
    //
    // MIERT? Korabban a ciklus MINDEN beerkezo parametert kiirt, szo
    // szerint. Az ismeretlen parametereket amugy sem dolgozzuk fel (a
    // konfiguraciot nem tudjak atirni), tehat diagnosztikai ertekuk nincs -
    // egy sok mezos POST viszont annyi sort irt volna a soros portra,
    // ahany mezot kuldtek, es mindezt az async_tcp taskban, ami kozben a
    // webszervert is kiszolgalja. A %.64s a tulmeretes ertekeket is
    // levagja, igy egyetlen sor sem nohet korlatlanul.
    //
    // A jelszót soha nem írjuk ki nyíltan a soros portra.
    if (name == PARAM_PASS) {
      Serial.printf("POST[%s]: <%u chars>\n", name.c_str(), (unsigned)val.length());
    } else if (name == PARAM_SSID || name == PARAM_IP || name == PARAM_GATEWAY) {
      Serial.printf("POST[%s]: %.64s\n", name.c_str(), val.c_str());
    }
  }

  // SSID nélkül az eszköz nem tudna hova csatlakozni: ilyet ne fogadjunk el
  // sikerként, mert az újraindulás után ugyanitt kötnénk ki.
  if (!d.ssidProvided) {
    saveOk = false;
    if (failReason == nullptr) failReason = "Hianyzo SSID.";
  }

  // Statikus IP-hez a gateway is kell. Az initWiFi() csak akkor konfigurál,
  // ha MINDKETTŐ értelmezhető - félig kitöltve csendben DHCP-re esne vissza,
  // a felhasználó viszont azt olvasná, hogy a megadott fix címen lesz.
  // A VÉGSŐ értékpárt nézzük: a most küldöttet, különben a mentettet.
  const char* ipFinal = d.ipSet ? d.ip : ipStr;
  const char* gwFinal = d.gwSet ? d.gateway : gatewayStr;
  if ((ipFinal[0] != '\0') != (gwFinal[0] != '\0')) {
    saveOk = false;
    if (failReason == nullptr) {
      failReason = "Statikus IP-hez az IP cimet ES a gateway-t is meg kell adni "
                   "(DHCP-hez hagyd mindkettot uresen).";
    }
  }

  return saveOk;
}

// 2. FAZIS: a mar ervenyesnek talalt draft kiirasa. A hivo felelossege, hogy
// a konfiguracios zar (beginConfigWrite) MAR a miénk legyen - ezert nem is
// szerzi meg es nem is engedi el: a zar elettartama a hivoe.
// Igaz, ha minden iras sikerult.
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
  return saveOk;
}

// A POST / kezeloje: a ket fazis osszefuzese es a HTTP valaszok.
static void handleConfigPost(AsyncWebServerRequest* request) {
  touchApDeadline();
  if (!filesystemReady()) {
    // Nincs értelme menteni: a fájlrendszer nem áll rendelkezésre.
    request->send(500, "text/plain",
                  "LittleFS nem elerheto, a beallitasok nem menthetok. "
                  "Ellenorizd a particios semat (partitions_custom.csv, "
                  "'spiffs' cimkeju particio).");
    return;
  }

  ConfigDraft draft;
  const char* failReason = nullptr;
  if (!parseConfigPost(request, draft, failReason)) {
    // Ne hazudjunk sikert és főleg ne indítsunk újra: az újraindítás
    // eldobná a beírt adatokat, a felhasználó pedig ugyanitt kötne ki.
    // Fájlt nem írtunk és globálist sem módosítottunk - minden a régi.
    touchApDeadline();
    Serial.println("A beallitasok mentese SIKERTELEN.");
    // A leghosszabb indoklás (statikus IP + gateway) a rögzített szöveggel
    // együtt 176 bájt; a snprintf() csonkolna, ha ennél kisebb lenne.
    char err[208];
    snprintf(err, sizeof(err),
               "A beallitasok mentese nem sikerult: %s "
               "Az eszkoz NEM indul ujra, probald meg ismet.",
               failReason ? failReason : "ismeretlen hiba.");
    request->send(500, "text/plain", err);
    return;
  }

  // --- 2. FÁZIS: minden mező érvényes -> zár, globálisok, fájlok.
  // A zár (savingConfig) atomikus megszerzése: a wifireset gomb törlése a
  // loop taskban ugyanezeket a fájlokat írná. Amíg a zár él, a loop() nem
  // altatja el az eszközt, és a gombok sem szólnak közbe.
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
    // Ide már csak tényleges fájlrendszer-hiba hozhat; ilyenkor a flash
    // tartalma lehet vegyes, de az eszköz nem indul újra, és a válasz
    // megmondja, mi történt.
    Serial.println("A beallitasok mentese SIKERTELEN.");
    request->send(500, "text/plain",
                    "A beallitasok mentese nem sikerult: LittleFS irasi hiba. "
                    "Az eszkoz NEM indul ujra, probald meg ismet.");
    return;
  }

  char message[96];
  if (ipStr[0] != '\0') {
    snprintf(message, sizeof(message),
             "Done. ESP will restart. Then go to IP address: %s", ipStr);
  } else {
    snprintf(message, sizeof(message),
             "Done. ESP will restart and connect to your router (DHCP).");
  }
  request->send(200, "text/plain", message);

  // Az async callbackben nem blokkolunk és nem indítunk újra:
  // a loop() teszi meg, miután a válasz kiment.
  logEvent(EV_CONFIG_SAVED, 0);
  requestRestart(RESTART_GRACE_MS);
}

// A /log kezeloje: a diagnosztikai naplo HTML oldala.
static void sendDiagnosticLog(AsyncWebServerRequest* request) {
  touchApDeadline();
  // A stream puffere igény szerint nő (resizeAdd), de akkor soronként
  // újraallokálna. Egy bőséges kezdőmérettel ez egyetlen foglalás lesz:
  // fejléc + állapot + 32 sor x ~70 bájt + a ~900 bájtos jelmagyarázat +
  // lábléc alatta marad.
  AsyncResponseStream* r = request->beginResponseStream("text/html", 6144);
  r->print(F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
             "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
             "<title>Naplo</title></head><body><h2>Diagnosztikai naplo</h2>"));

  const esp_reset_reason_t rr = esp_reset_reason();
  r->printf("<p><b>Utolso indulas oka:</b> %s (%d)<br>",
            resetReasonName(rr), (int)rr);
  // A negy szamlalot EGYSZERRE kerjuk le (app_hooks.h): igy a lapon egymassal
  // konzisztens pillanatkep jelenik meg, nem negy kulon idopillanate.
  const DiagCounters dc = diagCounters();
  r->printf("<b>Watchdog ujraindulasok:</b> %u / %u<br>",
            (unsigned)dc.wdtResets, (unsigned)dc.wdtLimit);
  r->printf("<b>Ujraprobalkozasi korok:</b> %u / %u<br>",
            (unsigned)dc.retryRounds, (unsigned)dc.retryLimit);
  // Az uptime ugyanabban az alakban, mint a soros porton - a nyers
  // masodpercbol (pl. "165600 mp") ranezesre semmi nem latszik.
  {
    const uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
    r->printf("<b>Uptime:</b> %ud %uh %um %us</p>",
              (unsigned)(up / 86400), (unsigned)((up % 86400) / 3600),
              (unsigned)((up % 3600) / 60), (unsigned)(up % 60));
  }

  // Pillanatkép a naplóról a mux alatt: az író (logEvent) a loop taskból
  // fut, ez a kezelő az async_tcp taskból - enélkül félig kiírt bejegyzést
  // is olvashatnánk. A kritikus szakasz egy memcpy.
  //
  // EGYETLEN puffert hasznalunk mindkét forráshoz (RTC és fájl): az
  // async_tcp task veremje véges, és 32 bejegyzés már 384 bájt. Előbb
  // eldöntjük, melyik forrás kell, és csak azt töltjük be.
  uint32_t evTotal;
  EventEntry evCopy[EVLOG_SIZE];
  portENTER_CRITICAL(&evLogMux);
  evTotal = (rtcEvMagic == EVLOG_MAGIC) ? rtcEvNext : 0;
  memcpy(evCopy, rtcEvents, sizeof(evCopy));
  portEXIT_CRITICAL(&evLogMux);

  // MELYIK A FRISSEBB? Az RTC naplo vagy a fajlba mentett?
  //
  // A fajl mindig az RTC naplo egy KORABBI pillanatkepe. Ebbol kovetkezik a
  // szabaly:
  //  - Ha az RTC naplo TULELTE a mentes ota eltelt idot (nem volt
  //    aramszunet), akkor bovebb is nala: mindent tartalmaz, ami a fajlban
  //    van, PLUSZ ami azota tortent. Ilyenkor az RTC nyer.
  //  - Ha az RTC naplot torolte egy aramszunet, a szamlalo nullarol indult,
  //    tehat a fajl tobb elozmenyt orzott meg. Ilyenkor a fajl nyer - es
  //    epp ez a mentes ertelme.
  //  - Ha viszont az RTC ota mar 32 UJ esemeny keletkezett, akkor a
  //    korpuffer teljesen tele van friss adattal, ami idoben mindenkeppen
  //    ujabb a fajlnal.
  //  - Ha MINDKETTONEK van valos ideje (NTP), az dont: a nagyobb idobelyeg
  //    nyer. Ez a legpontosabb valasz, ezert ez az elso szabaly.
  //
  // A dontes CSAK a fejlecbol tortenik, es a bejegyzeseket utana toltjuk be -
  // igy egyszerre csak EGY 384 bajtos puffer all a vermen.
  //
  // Ha epp fajliras folyik a masik taskbol, a fajlt NEM olvassuk. Ez a
  // kizaras nem atomi (az iras a kerdes utan is elindulhat), de nem is kell
  // annak lennie: egy felig kiirt fajlon a fejlec ellenorzese bukik, es a
  // lap ugyanoda jut - az RTC naplohoz.
  EvFileHeader fej;
  bool vanFajl = false;
  if (!configWriteInProgress() && loadEventLogHeader(fej)) {
    bool rtcNyer;
    if (evTotal == 0) {
      rtcNyer = false;                       // nincs mit mutatni az RTC-bol
    } else {
      // A fajl sajat idobelyege (mikor mentettuk) a helyes osszehasonlitasi
      // alap: pontosan azt mondja meg, mikori a tartalma.
      const uint32_t rtcEpoch = evCopy[(evTotal - 1) % EVLOG_SIZE].epoch;
      if (rtcEpoch >= NTP_MIN_VALID_EPOCH
          && fej.savedEpoch >= NTP_MIN_VALID_EPOCH) {
        rtcNyer = (rtcEpoch >= fej.savedEpoch);   // valos ido dont
      } else {
        rtcNyer = (evTotal >= fej.evNextAtSave) || (evTotal >= EVLOG_SIZE);
      }
    }
    if (!rtcNyer) {
      if (loadEventLogEntries(fej, evCopy)) {
        // A fajl bejegyzesei mar KIEGYENESITVE vannak (a legregebbitol a
        // legujabbig), es a puffer elejen allnak; az evTotal a darabszam
        // lesz, igy a lenti kiiro ciklus mindket forrasra ugyanaz.
        vanFajl = true;
        evTotal = fej.count;
      } else {
        // A BETOLTES FELUTON BUKOTT. Mivel EGY puffert hasznalunk, az
        // olvasas addigra mar felulirhatta az RTC pillanatkep elejet -
        // a puffer most fel fajl, fel RTC adat lenne. Ezert nem eleg
        // "visszalepni" az RTC-re: UJRA kell venni a pillanatkepet.
        portENTER_CRITICAL(&evLogMux);
        evTotal = (rtcEvMagic == EVLOG_MAGIC) ? rtcEvNext : 0;
        memcpy(evCopy, rtcEvents, sizeof(evCopy));
        portEXIT_CRITICAL(&evLogMux);
      }
    }
  }

  if (evTotal == 0) {
    // Sem az RTC-ben, sem a fajlban nincs semmi (vagy a fajl nem letezik,
    // ures, csonka). Ez nem hiba: egyszeruen nincs mit mutatni.
    r->print(F("<p>Nincs rogzitett esemeny.</p>"));
  } else {
    if (vanFajl) {
      char mikor[24];
      r->print(F("<p><b>Forras:</b> a fajlrendszerre mentett naplo"));
      if (formatEpoch(fej.savedEpoch, mikor, sizeof(mikor))) {
        r->printf(" (mentve: %s)", mikor);
      } else {
        // Nincs valos ido - de a mentes UPTIME-ja megvan. Abbol legalabb az
        // latszik, mennyi ideje futott az eszkoz, amikor a fajl keszult; ez
        // a "melyik esemenysor mikori?" kerdesre ora nelkul is valasz.
        // (A mezot eddig kiirtuk a fajlba, de sosem olvastuk vissza.)
        r->printf(" (mentve a bootolas utan %u:%02u:%02u-kor)",
                  (unsigned)(fej.savedUptime / 3600),
                  (unsigned)((fej.savedUptime % 3600) / 60),
                  (unsigned)(fej.savedUptime % 60));
      }
      r->print(F(" - az RTC naplo ennel regebbi vagy ures "
                 "(pl. aramszunet torolte).</p>"));
    } else {
      r->print(F("<p><b>Forras:</b> az RTC memoriaban levo naplo "
                 "(ez a frissebb).</p>"));
    }
    r->print(F("<table border=1 cellpadding=4><tr><th>Ido</th><th>Uptime</th>"
               "<th>Esemeny</th><th>Param</th></tr>"));
    // A legregebbi meg meglevo bejegyzestol indulunk. Fajlbol olvasva a
    // bejegyzesek mar sorban allnak, tehat a "% EVLOG_SIZE" ott is helyes.
    const uint32_t shown = evTotal < EVLOG_SIZE ? evTotal : EVLOG_SIZE;
    for (uint32_t i = evTotal - shown; i < evTotal; i++) {
      const EventEntry& e = evCopy[i % EVLOG_SIZE];
      char mikor[24];
      r->print(F("<tr><td>"));
      if (formatEpoch(e.epoch, mikor, sizeof(mikor))) {
        r->print(mikor);
      } else {
        // Nem volt (meg) oraszinkron ennel a bejegyzesnel. Nem hiba - a
        // uptime oszlop ilyenkor is elmond mindent.
        r->print(F("-"));
      }
      r->printf("</td><td>%u:%02u:%02u</td><td>%s</td><td>%u</td></tr>",
                (unsigned)(e.uptimeSec / 3600), (unsigned)((e.uptimeSec % 3600) / 60),
                (unsigned)(e.uptimeSec % 60), eventName(e.code), (unsigned)e.param);
    }
    r->print(F("</table>"));
  }
  // Jelmagyarazat: a Param oszlop szamai kulonben csak a forraskodbol
  // fejthetok meg - epp az az informacio veszne el, amiert az oldal van.
  // FIGYELEM a jelolesre: az esemenyneveket SZANDEKOSAN nem tesszuk <b> koze.
  // A tablazat cellai ">NEV<" alaku szoveget adnak, es a naplo tartalmara
  // pontosan erre a mintara lehet illeszteni (igy teszi az L2 teszt is).
  // Egy <b>BOOT</b> ugyanezt a mintat allitana elo a jelmagyarazatban, es
  // elmosna a kulonbseget "a naplóban VAN ilyen esemeny" es "a lap emliti
  // ezt az esemenyt" kozott.
  r->print(F("<h3>Param jelentese</h3><ul>"
             "<li>BOOT: az indulas oka (lasd fent)</li>"
             "<li>WIFI OK: hanyadik ujraprobalkozasi korben sikerult</li>"
             "<li>WIFI LOST: a WiFi.status() erteke</li>"
             "<li>TEST FAIL: a bukott teszt sorszama (1-5)</li>"
             "<li>ROUTER RESET: hanyadik ujrainditas (1-4)</li>"
             "<li>AP MODE: 1 = nincs SSID, 2 = rossz jelszo, "
             "3 = letelt a 2 nap, 4 = a gateway sem erheto el</li>"
             "<li>CONFIG SAVED: mindig 0 - itt maga a bejegyzes az "
             "informacio, parametere nincs</li>"
             "<li>GW UNREACH: 1 = a router reset elott, 2 = utana is</li>"
             "<li>SLEEP: 1 = ujraprobalkozas, 2 = tartos internetkieses, "
             "3 = AP idotullepes, 4 = vegzetes hiba</li>"
             "<li>FATAL: 1 = LittleFS, 2 = konfig olvasas, "
             "3 = watchdog, 4 = wifireset torles, 5 = tartosan keves heap</li>"
             "<li>WDT RESET: hanyadik rendellenes ujraindulas</li>"
             "<li>STUCK BUTTON: 0 = reset gomb, 1 = wifireset gomb</li>"
             "<li>LOW HEAP: a szabad heap KB-ban a kuszob atlepesekor</li>"
             "<li>HEAP RESTART: a szabad heap KB-ban az onkentes "
             "ujraindulas elott</li>"
             "</ul>"));
  r->print(F("<p><i>Az uptime minden indulaskor nullarol indul, ezert a "
             "BOOT sorok jelzik az ujraindulasokat. A naplo az "
             "aramtalanitast nem eli tul.</i></p>"
             "<p><a href=\"/\">Vissza a beallitasokhoz</a></p>"));
  // A naplót is lehet 5 percnél tovább olvasni - ne aludjon el közben.
  // Flashből megy a pufferbe, nem allokál külön.
  r->print(KEEPALIVE_JS);
  r->print(F("</body></html>"));
  request->send(r);
}

void startWebPortal() {
  Serial.println("Setting AP (Access Point)");
  char apName[32];
  snprintf(apName, sizeof(apName), "ESP-%s", ESP.getChipModel());
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName, AP_PASSWORD);
  Serial.print("AP SSID: ");
  Serial.println(apName);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  // Web Server Root URL. Az űrlap a programból megy ki, nem a LittleFS-ről:
  // nincs se feltöltendő data/ mappa, se olyan eset, hogy a fájlrendszer
  // állapota miatt ne lenne beállító felület.
  //
  // A LittleFS-ről szándékosan NEM szolgálunk ki semmit: egy serveStatic("/")
  // a teljes fájlrendszert kiadta volna, azaz a /pass.txt-ben tárolt Wi-Fi
  // jelszót is.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    touchApDeadline();
    sendConfigForm(request);
  });
  // Keep-alive. A nyitva lévő oldal 60 mp-enként meghívja, így az AP mód
  // visszaszámlálása addig tolódik, amíg tényleg ott vagy a lapon. A válasz
  // szándékosan egyetlen bájt: percenként fut, és a rádió a legdrágább.
  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest* request) {
    touchApDeadline();
    request->send(200, "text/plain", "1");
  });

  // Diagnosztikai naplo. Ez az egyetlen mod, hogy soros kabel nelkul megtudd,
  // mi tortent az eszkozzel - es epp AP modban vagy, amikor baj van.
  server.on("/log", HTTP_GET, [](AsyncWebServerRequest* request) {
    touchApDeadline();
    sendDiagnosticLog(request);
  });

  server.onNotFound([](AsyncWebServerRequest* request) {
    // A 404 is interakció: valaki épp az eszközzel dolgozik. A böngésző
    // magától kéri például a /favicon.ico-t, és egy elgépelt cím sem
    // jelenti azt, hogy a felhasználó elment. Enélkül a "minden HTTP kérés
    // kitolja a határidőt" szabály nem lenne igaz.
    touchApDeadline();
    request->send(404, "text/plain", "Not found");
  });

  server.on("/", HTTP_POST, [](AsyncWebServerRequest* request) {
    handleConfigPost(request);
  });

  server.begin();
}

void stopWebPortal() {
  server.end();
}
