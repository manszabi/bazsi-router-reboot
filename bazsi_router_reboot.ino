#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <HTTPClient.h>
#include <ESPping.h>
#include <string.h>
#include <ctype.h>
#include "LittleFS.h"
#include "esp_sleep.h"
#include "esp_idf_version.h"

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Search for parameter in HTTP POST request
const char* PARAM_SSID    = "ssid";
const char* PARAM_PASS    = "pass";
const char* PARAM_IP      = "ip";
const char* PARAM_GATEWAY = "gateway";

// Tartalék űrlap arra az esetre, ha a data/ mappa nincs feltöltve a LittleFS-re.
// Flashben él, RAM-ot nem foglal.
const char FALLBACK_FORM[] =
  "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
  "<title>ESP Wi-Fi Manager</title></head><body>"
  "<h2>ESP Wi-Fi Manager</h2>"
  "<p><b>Figyelem:</b> a data/ mappa nincs feltoltve a LittleFS-re.</p>"
  "<form action=\"/\" method=\"POST\">"
  "SSID <input name=\"ssid\" maxlength=\"32\" required><br>"
  "Password <input name=\"pass\" type=\"password\" maxlength=\"63\"><br>"
  "IP <input name=\"ip\" maxlength=\"15\" placeholder=\"opcionalis\"><br>"
  "Gateway <input name=\"gateway\" maxlength=\"15\" placeholder=\"opcionalis\"><br>"
  "<input type=\"submit\" value=\"Submit\"></form></body></html>";

// AP password (WPA2: min. 8 karakter)
const char* AP_PASSWORD = "bazsi1234";

// A HTML űrlapról érkező értékek. Fix méretű bufferek: nincs heap-töredezettség,
// és a szabvány szerinti maximumok egyben validációt is jelentenek.
constexpr size_t SSID_MAX_LEN  = 32;  // IEEE 802.11 SSID
constexpr size_t PASS_MAX_LEN  = 63;  // WPA2-PSK passphrase
constexpr size_t IPSTR_MAX_LEN = 15;  // "255.255.255.255"

char ssid[SSID_MAX_LEN + 1]        = { 0 };
char pass[PASS_MAX_LEN + 1]        = { 0 };
char ipStr[IPSTR_MAX_LEN + 1]      = { 0 };
char gatewayStr[IPSTR_MAX_LEN + 1] = { 0 };

// File paths to save input values permanently
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* ipPath = "/ip.txt";
const char* gatewayPath = "/gateway.txt";

IPAddress localIP;
IPAddress localGateway;
IPAddress subnet(255, 255, 255, 0);
// Tartalék DNS statikus IP esetén (a DHCP-től ilyenkor nem kapunk DNS-t)
IPAddress dnsFallback(1, 1, 1, 1);
// Ping célok: két különböző szolgáltató, hogy egyikük kiesése ne tűnjön
// internetkimaradásnak
IPAddress pingTargetCloudflare(1, 1, 1, 1);
IPAddress pingTargetGoogle(8, 8, 8, 8);

// strapping pins 2, 8, 9
// Set LED GPIO, relay state
constexpr uint8_t ledPin = D4;
// wifi ok led
constexpr uint8_t wifiledPin = D3;
// Set RELAY pin, to router
constexpr uint8_t relayPin = D5;
// Set reset pin, esp wifireset pin
constexpr uint8_t wifiresetPin = D0;
// Set reset pin, esp reset/wakeup pin
constexpr uint8_t resetPin = D1;

// Timer variables
// interval to wait for Wi-Fi connection (milliseconds)
constexpr uint32_t interval = 20 * 1000;
constexpr uint32_t SUCCESS_DELAY = 1 * 60 * 1000;
constexpr uint32_t PROBE_DELAY = 12 * 1000;
constexpr uint32_t RESET_DELAY = 10 * 60 * 1000;
constexpr uint32_t RESET_PULSE = 90 * 1000;
constexpr uint32_t firstStartDelay = 10 * 60 * 1000;
constexpr uint8_t maxfailureEvents = 5;  // failure sleep
constexpr uint8_t wifi_maxRetries = 3;
constexpr uint32_t wifiInterval = 20 * 1000;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;
constexpr uint32_t RESTART_GRACE_MS = 2000;  // válasz kiküldése újraindítás előtt

constexpr uint64_t SLEEP_DURATION_US = 3600ULL * 1000000ULL;      // 1 óra
constexpr uint64_t STUCK_BUTTON_SLEEP_US = 60ULL * 1000000ULL;    // 60 másodperc

// Teszt paraméterek
constexpr uint8_t PING_ATTEMPTS = 4;
constexpr uint8_t PING_MIN_SUCCESS = 2;
constexpr uint32_t PING_GAP_MS = 1000;
constexpr size_t HTTP_MAX_PAYLOAD = 96;  // a várt válaszok < 32 bájt
constexpr uint32_t HTTP_READ_TIMEOUT_MS = 1500;
constexpr uint8_t MAX_CYCLE_INDEX = 10;
constexpr uint8_t RESET_TRIGGER_FAILURES = 3;
constexpr uint8_t RESET_TRIGGER_CYCLE = 3;

struct TestState {
  uint8_t cycleIndex = 0;   // volt: i
  uint8_t failedCount = 0;  // volt: failedTestsCount
  uint8_t resetEvents = 0;  // volt: Nreset_events
  uint8_t resetStep = 0;    // 0 = tétlen, 1 = fut a reset pulzus
};

struct TimingState {
  uint32_t stateStart = 0;             // állapotgép: aktuális állapot kezdete
  uint32_t resetPulseStart = 0;        // relé kikapcsolás kezdete
  uint32_t startMillis = 0;            // setup() időbélyege
  uint32_t resetBtnDownSince = 0;      // debounce: mióta LOW a reset gomb
  uint32_t wifiResetBtnDownSince = 0;  // debounce: mióta LOW a wifireset gomb
};

struct UIFlags {
  bool successPrinted = false;     // volt: successfulTestPrinted
  bool resetPrinted = false;       // volt: beginResetPrinted
  bool firstStartPrinted = false;  // volt: firstStartPrinted
  bool firstStart = true;          // volt: firstStart
};

TestState testState;
TimingState timing;
UIFlags uiFlags;

enum State : uint8_t {
  TESTING_STATE = 0,
  FAILURE_STATE = 1,
  SUCCESS_STATE = 2
};

// Az eszköz vagy a routert figyeli, vagy a Wi-Fi beállító portált szolgálja ki.
enum DeviceMode : uint8_t {
  MODE_MONITOR = 0,
  MODE_CONFIG = 1
};

State currentState = TESTING_STATE;
DeviceMode deviceMode = MODE_MONITOR;

// Az aszinkron webszerver callbackjéből nem szabad blokkolni/újraindítani,
// ezért csak jelzünk, az újraindítást a loop() végzi el.
volatile bool restartPending = false;
volatile uint32_t restartAt = 0;

// Forward declarations (a .ino auto-prototípusok helyett explicit módon)
void printUptime();
void resetbutton();
void wifiresetbutton();
void blockingDelay(uint32_t duration);
void waitWithButtons(uint32_t duration);
void tosleep();
bool initWiFi();
bool reconnectWifi();

// Whitespace levágása helyben, allokáció nélkül
void trimInPlace(char* s) {
  size_t len = strlen(s);
  while (len > 0 && isspace((unsigned char)s[len - 1])) {
    s[--len] = '\0';
  }
  size_t start = 0;
  while (s[start] != '\0' && isspace((unsigned char)s[start])) {
    start++;
  }
  if (start > 0) {
    memmove(s, s + start, len - start + 1);
  }
}

// Initialize LittleFS
bool initLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("An error has occurred while mounting LittleFS");
    return false;
  }
  Serial.println("LittleFS mounted successfully");
  return true;
}

// Egy konfigurációs érték beolvasása fix méretű bufferbe (String allokáció nélkül)
bool readConfigValue(fs::FS& fs, const char* path, char* out, size_t outSize) {
  out[0] = '\0';
  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.printf("- failed to open %s for reading\r\n", path);
    if (file) {
      file.close();
    }
    return false;
  }
  // Méret szerint olvasunk: a Stream::readBytesUntil() EOF-nál kivárná a teljes
  // 1 másodperces stream-timeoutot, fájlonként (indulásnál ez 4 mp veszteség).
  const size_t fileSize = file.size();
  const size_t toRead = (fileSize < outSize - 1) ? fileSize : outSize - 1;
  const size_t n = file.read((uint8_t*)out, toRead);
  out[n] = '\0';
  file.close();

  char* nl = strchr(out, '\n');  // csak az első sor érdekel
  if (nl != nullptr) {
    *nl = '\0';
  }
  trimInPlace(out);
  return out[0] != '\0';
}

void clearFile(fs::FS& fs, const char* path) {
  Serial.printf("Clearing file: %s\r\n", path);
  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for clearing");
    return;
  }
  file.close();
  Serial.println("- file cleared");
}

// Write file to LittleFS
void writeFile(fs::FS& fs, const char* path, const char* message) {
  Serial.printf("Writing file: %s\r\n", path);
  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- write failed or empty file");
  }
  file.close();
}

void blockingDelay(uint32_t duration) {
  const uint32_t start = millis();
  while (millis() - start < duration) {
    yield();
  }
}

// Várakozás úgy, hogy a fizikai gombok közben is működnek
void waitWithButtons(uint32_t duration) {
  const uint32_t start = millis();
  while (millis() - start < duration) {
    resetbutton();
    wifiresetbutton();
    yield();
  }
}

void handleStuckButton(const char* message) {
  Serial.println(message);
  digitalWrite(ledPin, LOW);
  Serial.flush();
  esp_sleep_enable_timer_wakeup(STUCK_BUTTON_SLEEP_US);
  esp_deep_sleep_start();
}

void printUptime() {
  const uint32_t totalSec = (uint32_t)(esp_timer_get_time() / 1000000);
  const uint32_t d = totalSec / 86400;
  const uint32_t h = (totalSec % 86400) / 3600;
  const uint32_t m = (totalSec % 3600) / 60;
  const uint32_t s = totalSec % 60;

  char buf[48];
  if (d > 0) {
    snprintf(buf, sizeof(buf), "Uptime: %lud %luh %lum %lus",
             (unsigned long)d, (unsigned long)h, (unsigned long)m, (unsigned long)s);
  } else {
    snprintf(buf, sizeof(buf), "Uptime: %luh %lum %lus",
             (unsigned long)h, (unsigned long)m, (unsigned long)s);
  }
  Serial.println(buf);
}

// Initialize WiFi
bool initWiFi() {
  printUptime();
  if (ssid[0] == '\0') {
    Serial.println("Undefined SSID!");
    return false;
  }

  // Ha időközben (pl. az ESP saját auto-reconnectje miatt) már él a kapcsolat,
  // ne indítsuk újra: a WiFi.begin() feleslegesen lebontaná.
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Already connected, skipping reconnect.");
    return true;
  }

  // A módot a config() előtt kell beállítani, különben egyes core verziókban
  // az "STA Failed to configure" hibával elszáll.
  WiFi.mode(WIFI_STA);

  // Statikus IP csak akkor, ha meg is adták. Üres mező = DHCP, nem hiba.
  bool staticOk = false;
  if (ipStr[0] != '\0' || gatewayStr[0] != '\0') {
    const bool ipValid = localIP.fromString(ipStr);
    const bool gatewayValid = localGateway.fromString(gatewayStr);
    if (!ipValid) {
      Serial.println("❌ Invalid IP format!");
    }
    if (!gatewayValid) {
      Serial.println("❌ Invalid gateway format!");
    }
    staticOk = ipValid && gatewayValid;
  }

  if (staticOk) {
    // DNS-t is meg kell adni: statikus konfignál a DHCP-s DNS elveszik,
    // enélkül a névfeloldás (és így a HTTP teszt) mindig elbukna.
    if (!WiFi.config(localIP, localGateway, subnet, localGateway, dnsFallback)) {
      Serial.println("⚠️ STA Failed to configure");
    } else {
      Serial.println("✅ Manual IP config applied.");
    }
  } else {
    Serial.println("➡️ Skipping manual IP config. Using DHCP...");
  }

  WiFi.begin(ssid, pass);
  Serial.println("Connecting to WiFi...");
  Serial.print("Trying to connect to SSID: ");
  Serial.println(ssid);

  const uint32_t startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED) {
    resetbutton();
    wifiresetbutton();
    yield();
    if (millis() - startAttempt >= interval) {
      printUptime();
      Serial.println("Failed to connect.");
      return false;
    }
  }

  printUptime();
  Serial.print("Connected to: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Signal strength (RSSI): ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  return true;
}

bool reset_device() {
  if (testState.resetStep == 0) {
    testState.resetEvents++;
    if (testState.resetEvents >= maxfailureEvents) {
      tosleep();
    }
    Serial.println("Router resetting");
    Serial.print("Powering OFF the router. Instance = ");
    Serial.println(testState.resetEvents);
    digitalWrite(relayPin, HIGH);
    Serial.println("Relay on.");
    digitalWrite(ledPin, LOW);
    Serial.println("Reset_pulse delay.");
    testState.resetStep = 1;
    timing.resetPulseStart = millis();
    // A pulzus most indult: ebben a hívásban még biztosan nem járt le.
    return false;
  }

  if (millis() - timing.resetPulseStart >= RESET_PULSE) {
    Serial.println("Reset_pulse delay end.");
    Serial.print("Powering ON the router. Instance = ");
    Serial.println(testState.resetEvents);
    digitalWrite(relayPin, LOW);
    digitalWrite(ledPin, HIGH);
    printUptime();
    testState.resetStep = 0;
    return true;
  }
  return false;
}

// FIGYELEM (hardver): deep sleep alatt az ESP32-C3 digitális lábai (GPIO6-21)
// nagyimpedanciás állapotba kerülnek, és csak az RTC lábak (GPIO0-5) tarthatók
// meg hold funkcióval. A relé a D5 = GPIO7-en van, tehát az alvás teljes ideje
// alatt LEBEG - szoftverből nem tartható. A relé vezérlőbemenetére külső
// lehúzó ellenállás kell (aktív-HIGH modulnál 10k GND felé), különben az
// alvás alatt véletlenül áramtalaníthatja a routert.
void tosleep() {
  digitalWrite(ledPin, LOW);  //led gnd, led off
  digitalWrite(relayPin, LOW);
  digitalWrite(wifiledPin, LOW);
  printUptime();
  Serial.print("Failed ");
  Serial.print(maxfailureEvents);
  Serial.println(" NCSI activity test, or WIFI disconnected, go to sleep ESP32-C3 device.");
  Serial.println("Going to sleep now");
  WiFi.disconnect(true);
  server.end();
  Serial.flush();
  Serial.end();

  esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
  // A reset gomb ébressze is fel az eszközt, ne csak az 1 órás timer.
  // Csak RTC-képes láb használható (ESP32-C3: GPIO0-GPIO5); a resetPin a
  // XIAO ESP32-C3-on D1 = GPIO3, tehát megfelel.
  // Szándékosan NINCS itt a wifireset gomb, és a handleStuckButton() sem
  // armol gombébresztést: egy beragadt gomb így nem tud boot loopot okozni.
  // Az IDF 6.0 átnevezte ezt az API-t, az arduino-esp32 pedig idf ">=5.3,<6.2"
  // tartományt deklarál, tehát mindkét névvel találkozhatunk.
#if ESP_IDF_VERSION_MAJOR >= 6
  esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL << resetPin, ESP_GPIO_WAKEUP_GPIO_LOW);
#else
  esp_deep_sleep_enable_gpio_wakeup(1ULL << resetPin, ESP_GPIO_WAKEUP_GPIO_LOW);
#endif
#endif
  esp_deep_sleep_start();
}

void resetbutton() {
  if (digitalRead(resetPin) != LOW) {
    timing.resetBtnDownSince = 0;  // felengedve: debounce újraindul
    return;
  }
  const uint32_t now = millis();
  if (timing.resetBtnDownSince == 0) {
    timing.resetBtnDownSince = now;
    return;
  }
  // Csak akkor fogadjuk el, ha végig lenyomva maradt (valódi debounce)
  if (now - timing.resetBtnDownSince >= BUTTON_DEBOUNCE_MS) {
    Serial.println("Reset button pressed.");
    Serial.println("RESTART ESP32C3 device.");
    Serial.flush();
    ESP.restart();
  }
}

void wifiresetbutton() {
  if (digitalRead(wifiresetPin) != LOW) {
    timing.wifiResetBtnDownSince = 0;
    return;
  }
  const uint32_t now = millis();
  if (timing.wifiResetBtnDownSince == 0) {
    timing.wifiResetBtnDownSince = now;
    return;
  }
  if (now - timing.wifiResetBtnDownSince >= BUTTON_DEBOUNCE_MS) {
    Serial.println("WIFIRESET button is pulling down!");
    Serial.println("RESET saved wifi data!");
    clearFile(LittleFS, gatewayPath);
    clearFile(LittleFS, ipPath);
    clearFile(LittleFS, passPath);
    clearFile(LittleFS, ssidPath);
    Serial.println("RESTART ESP32C3 device.");
    Serial.flush();
    ESP.restart();
  }
}

bool reconnectWifi() {
  printUptime();
  Serial.println("Starting reconnectWifi loop");

  for (uint8_t attempt = 0; attempt < wifi_maxRetries; attempt++) {
    printUptime();
    Serial.print("Attempt ");
    Serial.println(attempt + 1);

    if (initWiFi()) {
      printUptime();
      Serial.println("WIFI RECONECTED!");
      digitalWrite(wifiledPin, HIGH);
      return true;
    }

    printUptime();
    Serial.print("WIFI ERROR! WiFi status: ");
    Serial.println(WiFi.status());

    if (attempt + 1 < wifi_maxRetries) {
      printUptime();
      Serial.print(wifiInterval / 1000);
      Serial.println(" seconds delay start.");
      waitWithButtons(wifiInterval);  // gombok közben is élnek, nincs busy-loop
    }
  }

  printUptime();
  Serial.print("WIFI FAILED TO RECONNECT AFTER ");
  Serial.print(wifi_maxRetries);
  Serial.println(" wifi_ATTEMPTS!");
  tosleep();
  return false;  // ha a tosleep() mégis visszatérne
}

// Korlátozott méretű, időzáras olvasás: nem allokál, és nem tud "elszállni"
// egy captive portal többszáz kilobájtos válaszán.
size_t readBounded(WiFiClient& stream, char* buf, size_t maxLen, uint32_t timeoutMs) {
  size_t n = 0;
  uint32_t lastByte = millis();
  while (n < maxLen && (millis() - lastByte) < timeoutMs) {
    const int c = stream.read();
    if (c < 0) {
      // A szerver lezárta és nincs több adat: nincs értelme a timeoutot kivárni
      if (!stream.connected() && stream.available() <= 0) {
        break;
      }
      resetbutton();
      wifiresetbutton();
      yield();
      continue;
    }
    buf[n++] = (char)c;
    lastByte = millis();
  }
  return n;
}

bool testInternetHTTP(const char* url, const char* expectedResponse) {
  WiFiClient client;
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(5000);
  http.setTimeout(10000);

  if (!http.begin(client, url)) {
    Serial.println("Error: HTTP begin failed");
    return false;
  }

  const int httpCode = http.GET();
  bool result = false;

  if (httpCode == HTTP_CODE_OK) {
    const int len = http.getSize();
    if (len > (int)HTTP_MAX_PAYLOAD) {
      // Ekkora választ nem a várt endpoint küld (pl. captive portal)
      Serial.print("Unexpected payload size: ");
      Serial.println(len);
    } else {
      const size_t want = (len > 0) ? (size_t)len : HTTP_MAX_PAYLOAD;
      char payload[HTTP_MAX_PAYLOAD + 1];
      // Ugyanaz a stream, amit a http.begin() kapott — nem függünk a
      // getStream() core-verziónként eltérő visszatérési típusától.
      const size_t n = readBounded(client, payload, want, HTTP_READ_TIMEOUT_MS);
      payload[n] = '\0';
      trimInPlace(payload);  // a záró CR/LF ne buktassa el az egyezést
      Serial.println(payload);
      result = (strcmp(payload, expectedResponse) == 0);
      Serial.println(result ? "Igaz érték!" : "Hamis érték!");
    }
  } else {
    Serial.print("Error on HTTP request, code: ");
    Serial.println(httpCode);
  }

  http.end();
  return result;
}

bool testInternetPing(IPAddress& target, const char* targetName) {
  Serial.print("Ping teszt futtatása (");
  Serial.print(targetName);
  Serial.print(" - ");
  Serial.print(target);
  Serial.println(")...");
  uint8_t successCount = 0;

  for (uint8_t j = 0; j < PING_ATTEMPTS; j++) {
    const bool pingOK = Ping.ping(target, 1);  // 1 próbálkozás pingenként
    Serial.print("Ping ");
    Serial.print(j + 1);
    if (pingOK) {
      Serial.println(" sikeres.");
      successCount++;
      // Az eredmény eldőlt, a maradék pinget felesleges megvárni
      if (successCount >= PING_MIN_SUCCESS) {
        Serial.println("✅ Ping teszt sikeres.");
        return true;
      }
    } else {
      Serial.println(" sikertelen.");
      if (j == 0) {
        Serial.println("⚠️ Első ping hiba — lehet, hogy a hálózat ébred.");
      }
      const uint8_t remaining = PING_ATTEMPTS - (j + 1);
      if (successCount + remaining < PING_MIN_SUCCESS) {
        Serial.println("❌ Ping teszt sikertelen — hálózati probléma valószínű.");
        return false;
      }
    }
    if (j + 1 < PING_ATTEMPTS) {
      waitWithButtons(PING_GAP_MS);  // Kíméletes tesztelés
    }
  }

  return successCount >= PING_MIN_SUCCESS;
}

void handleFirstStart(uint32_t currentMillis) {
  if (WiFi.status() != WL_CONNECTED) {
    if (currentMillis - timing.startMillis < firstStartDelay) {
      if (!uiFlags.firstStartPrinted) {
        printUptime();
        Serial.println("First start wait begin.");
        uiFlags.firstStartPrinted = true;
      }
      resetbutton();
      wifiresetbutton();
      yield();
      return;  // csak itt kilép, visszaadja a vezérlést a loop()-nak
    }

    printUptime();
    Serial.println("First start wait end.");

    if (!reconnectWifi()) {
      return;  // a következő loop()-körben újrapróbáljuk
    }

    timing.startMillis = millis();  // újraindítjuk az időzítést (friss bélyeg)
  }
  uiFlags.firstStart = false;
}

void startConfigPortal() {
  deviceMode = MODE_CONFIG;
  digitalWrite(wifiledPin, LOW);  //led off

  Serial.println("Setting AP (Access Point)");
  char apName[32];
  snprintf(apName, sizeof(apName), "ESP-%s", ESP.getChipModel());
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName, AP_PASSWORD);
  Serial.print("AP SSID: ");
  Serial.println(apName);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  // Web Server Root URL. Ha a data/ mappa nem került fel a LittleFS-re, a
  // beginResponse(FS&,...) NULL-t ad és a kliens 501-et kapna - ilyenkor az
  // eszköz konfigurálhatatlan lenne, ezért beépített tartalék űrlapot adunk.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (LittleFS.exists("/wifimanager.html")) {
      request->send(LittleFS, "/wifimanager.html", "text/html");
    } else {
      request->send(200, "text/html", FALLBACK_FORM);
    }
  });

  // Csak a weboldal statikus elemeit szolgáljuk ki. A serveStatic("/") a teljes
  // LittleFS-t kiadta volna, azaz a /pass.txt-ben tárolt Wi-Fi jelszót is.
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(LittleFS, "/style.css", "text/css");
  });
  server.on("/favicon.png", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(LittleFS, "/favicon.png", "image/png");
  });
  server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "Not found");
  });

  server.on("/", HTTP_POST, [](AsyncWebServerRequest* request) {
    const int params = request->params();
    for (int i = 0; i < params; i++) {
      const AsyncWebParameter* p = request->getParam(i);
      if (!p->isPost()) {
        continue;
      }
      const String& name = p->name();
      const String& val = p->value();  // referencia: nincs felesleges másolat

      if (name == PARAM_SSID) {
        if (val.length() > 0 && val.length() <= SSID_MAX_LEN) {
          strlcpy(ssid, val.c_str(), sizeof(ssid));
          Serial.print("SSID set to: ");
          Serial.println(ssid);
          writeFile(LittleFS, ssidPath, ssid);
        } else {
          Serial.println("Invalid SSID length!");
        }
      } else if (name == PARAM_PASS) {
        if (val.length() <= PASS_MAX_LEN) {
          strlcpy(pass, val.c_str(), sizeof(pass));
          Serial.print("Password set to: ");
          Serial.print(val.length());
          Serial.println(" chars");
          writeFile(LittleFS, passPath, pass);
        } else {
          Serial.println("Password too long!");
        }
      } else if (name == PARAM_IP) {
        IPAddress testIP;
        if (val.length() == 0) {
          ipStr[0] = '\0';
          Serial.println("IP empty, using DHCP.");
          writeFile(LittleFS, ipPath, "");
        } else if (val.length() <= IPSTR_MAX_LEN && testIP.fromString(val.c_str())) {
          strlcpy(ipStr, val.c_str(), sizeof(ipStr));
          Serial.print("IP Address set to: ");
          Serial.println(ipStr);
          writeFile(LittleFS, ipPath, ipStr);
        } else {
          Serial.println("Invalid IP format!");
        }
      } else if (name == PARAM_GATEWAY) {
        IPAddress testIP;
        if (val.length() == 0) {
          gatewayStr[0] = '\0';
          Serial.println("Gateway empty, using DHCP.");
          writeFile(LittleFS, gatewayPath, "");
        } else if (val.length() <= IPSTR_MAX_LEN && testIP.fromString(val.c_str())) {
          strlcpy(gatewayStr, val.c_str(), sizeof(gatewayStr));
          Serial.print("Gateway set to: ");
          Serial.println(gatewayStr);
          writeFile(LittleFS, gatewayPath, gatewayStr);
        } else {
          Serial.println("Invalid gateway format!");
        }
      }

      // A jelszót soha nem írjuk ki nyíltan a soros portra
      if (name == PARAM_PASS) {
        Serial.printf("POST[%s]: <%u chars>\n", name.c_str(), (unsigned)val.length());
      } else {
        Serial.printf("POST[%s]: %s\n", name.c_str(), val.c_str());
      }
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
    restartAt = millis() + RESTART_GRACE_MS;
    restartPending = true;
  });

  server.begin();
}

void setup() {
  timing.startMillis = millis();

  pinMode(wifiresetPin, INPUT_PULLUP);
  pinMode(resetPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  pinMode(wifiledPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  digitalWrite(wifiledPin, LOW);
  digitalWrite(relayPin, LOW);
  digitalWrite(ledPin, HIGH);  //bekapcsolja a ledet, +5volt 150 ohm

  Serial.begin(115200);
  const uint32_t serialTimeout = millis();
  while (!Serial && millis() - serialTimeout < 3000) { yield(); }
  blockingDelay(500);  // USB CDC beállása, hogy az induló logok ne vesszenek el

  printUptime();

  if (digitalRead(resetPin) == LOW) {
    handleStuckButton("Reset button got stuck.");
  }
  if (digitalRead(wifiresetPin) == LOW) {
    handleStuckButton("Wifireset button got stuck.");
  }

  Serial.println("Init LittleFS.");
  const bool fsReady = initLittleFS();
  // Load values saved in LittleFS
  readConfigValue(LittleFS, ssidPath, ssid, sizeof(ssid));
  readConfigValue(LittleFS, passPath, pass, sizeof(pass));
  readConfigValue(LittleFS, ipPath, ipStr, sizeof(ipStr));
  readConfigValue(LittleFS, gatewayPath, gatewayStr, sizeof(gatewayStr));

  Serial.println(ssid);
  Serial.print((unsigned)strlen(pass));
  Serial.println(" chars password loaded");
  Serial.println(ipStr);
  Serial.println(gatewayStr);

  // Ne írjuk a hitelesítő adatokat minden WiFi.begin()-nél az NVS-be (flash kímélés)
  WiFi.persistent(false);

  if (initWiFi()) {
    Serial.println("WIFI OK!");
    deviceMode = MODE_MONITOR;
    digitalWrite(wifiledPin, HIGH);  //led on
  } else {
    // Nincs használható Wi-Fi: konfigurációs portál AP módban.
    if (!fsReady) {
      Serial.println("⚠️ LittleFS mount failed - the config page cannot be served!");
    }
    startConfigPortal();
  }
}

void loop() {
  const uint32_t currentMillis = millis();

  // Az AP-módú beállító oldal kérésére halasztott újraindítás
  if (restartPending && (int32_t)(currentMillis - restartAt) >= 0) {
    restartPending = false;
    Serial.println("RESTART!");
    Serial.flush();
    ESP.restart();
  }

  if (deviceMode == MODE_CONFIG) {
    // Konfig módban nincs internetteszt és nincs elalvás: a portálnak
    // életben kell maradnia, amíg a felhasználó be nem küldi az adatokat.
    resetbutton();
    wifiresetbutton();
    yield();
    return;
  }

  if (uiFlags.firstStart) {
    handleFirstStart(currentMillis);
    return;  // így biztosan nem fut le semmi más ebben a körben
  }

  resetbutton();
  wifiresetbutton();

  switch (currentState) {

    case TESTING_STATE: {
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected before test!");
        digitalWrite(wifiledPin, LOW);
        currentState = FAILURE_STATE;
        timing.stateStart = currentMillis;
        testState.failedCount++;
        break;
      }
      // A kapcsolat magától is helyreállhat (auto-reconnect), ilyenkor a LED
      // korábban hazudott volna.
      digitalWrite(wifiledPin, HIGH);
      printUptime();
      Serial.println("Beginning Test.");
      Serial.print("Teszt ciklus index = ");
      Serial.print(testState.cycleIndex);
      Serial.print(" | Hibák száma = ");
      Serial.println(testState.failedCount);

      bool testResult;
      if (testState.cycleIndex == 1) {
        testResult = testInternetPing(pingTargetCloudflare, "Cloudflare");
      } else if (testState.cycleIndex == 3) {
        testResult = testInternetPing(pingTargetGoogle, "Google");
      } else if (testState.cycleIndex == 2 || testState.cycleIndex == 4) {
        testResult = testInternetHTTP("http://www.msftncsi.com/ncsi.txt", "Microsoft NCSI");
      } else {
        testResult = testInternetHTTP("http://www.msftconnecttest.com/connecttest.txt", "Microsoft Connect Test");
      }

      if (testResult) {
        testState.cycleIndex = 0;
        testState.failedCount = 0;
        testState.resetEvents = 0;
        currentState = SUCCESS_STATE;
      } else {
        testState.failedCount++;
        printUptime();
        Serial.println("Test failed.");
        currentState = FAILURE_STATE;
      }
      // A tesztek percekig futhatnak, ezért friss időbélyeg kell.
      timing.stateStart = millis();
      break;
    }

    case FAILURE_STATE:
      if (testState.cycleIndex > RESET_TRIGGER_CYCLE && testState.failedCount >= RESET_TRIGGER_FAILURES) {

        if (!uiFlags.resetPrinted) {
          printUptime();
          Serial.println("Begining Reset in FAILURE_STATE.");
          while (!reset_device()) {
            resetbutton();
            wifiresetbutton();
            yield();
          }
          printUptime();
          Serial.println("Reset is done in FAILURE_STATE.");
          Serial.println("RESET_DELAY start in FAILURE_STATE.");
          timing.stateStart = millis();
          uiFlags.resetPrinted = true;  // Set the flag after printing
          break;                        // a RESET_DELAY a következő körökben telik
        }

        if (millis() - timing.stateStart >= RESET_DELAY) {
          printUptime();
          Serial.println("RESET_DELAY end in FAILURE_STATE.");
          Serial.println("Reconnect WIFI in FAILURE_STATE.");
          WiFi.disconnect(true);
          blockingDelay(100);

          // initWiFi()-t hívunk nyers WiFi.begin() helyett: a disconnect(true)
          // leállítja a WiFi-t, és ilyenkor a netif statikus IP/DNS beállítása
          // elveszik. Az initWiFi() újra alkalmazza, mielőtt csatlakozna.
          if (initWiFi()) {
            printUptime();
            Serial.println("WIFI OK in FAILURE_STATE.");
            digitalWrite(wifiledPin, HIGH);
          } else {
            printUptime();
            Serial.println("WIFI fail, Restart WIFI in FAILURE_STATE.");
            digitalWrite(wifiledPin, LOW);
            if (!reconnectWifi()) {
              break;  // a következő körben újrapróbáljuk
            }
          }

          testState.cycleIndex = 0;
          testState.failedCount = 0;
          uiFlags.resetPrinted = false;
          currentState = TESTING_STATE;
        }

      } else {
        if (currentMillis - timing.stateStart >= PROBE_DELAY) {
          if (testState.cycleIndex < MAX_CYCLE_INDEX) {
            testState.cycleIndex++;
          }
          currentState = TESTING_STATE;
        }
      }
      break;

    case SUCCESS_STATE:

      if (!uiFlags.successPrinted) {
        printUptime();
        Serial.println("Successful Test");
        Serial.println();
        Serial.println("SUCCESS_DELAY delay start.");
        uiFlags.successPrinted = true;  // Set the flag to true after printing
      }

      if (currentMillis - timing.stateStart >= SUCCESS_DELAY) {
        printUptime();
        Serial.println("SUCCESS_DELAY delay end.");
        uiFlags.successPrinted = false;
        timing.stateStart = currentMillis;
        currentState = TESTING_STATE;
      }
      break;
  }
}
