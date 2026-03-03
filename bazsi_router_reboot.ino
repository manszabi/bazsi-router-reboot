#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "LittleFS.h"
#include "esp_wifi.h"
#include <HTTPClient.h>
#include <ESPping.h>

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Search for parameter in HTTP POST request
const char* PARAM_SSID    = "ssid";
const char* PARAM_PASS    = "pass";
const char* PARAM_IP      = "ip";
const char* PARAM_GATEWAY = "gateway";

// AP password
const char* AP_PASSWORD = "bazsi1234";

//Variables to save values from HTML form
String ssid;
String pass;
String ip;
String gateway;

// File paths to save input values permanently
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* ipPath = "/ip.txt";
const char* gatewayPath = "/gateway.txt";

IPAddress localIP;
//IPAddress localIP(192, 168, 1, 200); // hardcoded

// Set your Gateway IP address
IPAddress localGateway;

//IPAddress localGateway(192, 168, 1, 1); //hardcoded
IPAddress subnet(255, 255, 255, 0);

// strapping pins 2, 8, 9
// Set LED GPIO, relay state
const int ledPin = D4;
// wifi ok led
const int wifiledPin = D3;
// Set RELAY pin, to router
const int relayPin = D5;
// Set reset pin, esp wifireset pin
const int wifiresetPin = D0;
// Set reset pin, esp reset/wakeup pin
const int resetPin = D1;

// #define BUTTON_PIN_BITMASK 0x200000000  // 2^33 in hex

// Timer variables
// interval to wait for Wi-Fi connection (milliseconds)
const uint32_t interval = 20 * 1000;
const uint32_t SUCCESS_DELAY = 1 * 60 * 1000;
const uint32_t PROBE_DELAY = 12 * 1000;
const uint32_t RESET_DELAY = 6 * 60 * 1000;
const uint32_t RESET_PULSE = 90 * 1000;
const uint32_t firstStartDelay = 3 * 60 * 1000;
const uint8_t maxfailureEvents = 5;  // failure sleep
const uint8_t wifi_maxRetries = 3;
const uint32_t wifiInterval = 20 * 1000;
const unsigned long debounceDelay_resetPin = 50;
const unsigned long debounceDelay_wifiresetPin = 50;

struct TestState {
  uint8_t cycleIndex = 0;       // volt: i
  int failedCount = 0;          // volt: failedTestsCount
  uint8_t resetEvents = 0;      // volt: Nreset_events
  bool resetPulseActive = false;
  uint8_t resetStep = 0;        // volt: step_reset_device
};

struct TimingState {
  unsigned long stateStart = 0;           // volt: stateStartMillis_loop
  unsigned long currentLoop = 0;          // volt: currentMillis_loop
  unsigned long resetPulseStart = 0;      // volt: previousMillisResetPulse_reset_device
  unsigned long resetDeviceCurrent = 0;   // volt: currentMillis_reset_device
  unsigned long wifiInitPrev = 0;         // volt: previousMillis_initWiFi
  unsigned long wifiInitCurrent = 0;      // volt: currentMillis_initWiFi
  unsigned long reconnectStart = 0;       // volt: startAttemptTime_reconnectWifi
  unsigned long reconnectCurrent = 0;     // volt: currentMillis_reconnectWifi
  unsigned long resetBtnDebounce = 0;     // volt: lastDebounceTime_resetPin
  unsigned long wifiResetBtnDebounce = 0; // volt: lastDebounceTime_wifiresetPin
  unsigned long resetBtnCurrent = 0;      // volt: currentMillis_resetbutton
  unsigned long wifiResetBtnCurrent = 0;  // volt: currentMillis_wifiresetbutton
  unsigned long startMillis = 0;          // volt: startMillis (set to millis() in setup)
};

struct UIFlags {
  bool successPrinted = false;      // volt: successfulTestPrinted
  bool resetPrinted = false;        // volt: beginResetPrinted
  bool firstStartPrinted = false;   // volt: firstStartPrinted
  bool wifiAttemptPrinted = false;  // volt: printAttempts
  bool firstStart = true;           // volt: firstStart
};

struct WifiState {
  bool connected = false;           // volt: wifiConnected
  bool connectionSuccess = false;   // volt: connectionSuccess
  int attempts = 0;                 // volt: wifi_attempts
  int buttonStateReset = HIGH;      // volt: buttonState_resetPin
  int buttonStateWifiReset = HIGH;  // volt: buttonState_wifiresetPin
};

TestState testState;
TimingState timing;
UIFlags uiFlags;
WifiState wifiState;

enum State : uint8_t {
  TESTING_STATE = 0,
  FAILURE_STATE = 1,
  SUCCESS_STATE = 2
};

State currentState = TESTING_STATE;

// Initialize LittleFS
void initLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("An error has occurred while mounting LittleFS");
  }
  Serial.println("LittleFS mounted successfully");
}

// Read File from LittleFS
String readFile(fs::FS& fs, const char* path) {
  Serial.printf("Reading file: %s\r\n", path);
  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return String();
  }
  String fileContent;
  while (file.available()) {
    fileContent = file.readStringUntil('\n');
    break;
  }
  file.close();
  return fileContent;
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

void blockingDelay(unsigned long duration) {
  unsigned long start = millis();
  while (millis() - start < duration) {
    yield();
  }
}

void handleStuckButton(const char* message) {
  Serial.println(message);
  digitalWrite(ledPin, LOW);
  esp_sleep_enable_timer_wakeup(60ULL * 1000000ULL);  // 60 sec
  esp_deep_sleep_start();
}

// Initialize WiFi
bool initWiFi() {

  printUptime();
  bool ssidValid = ssid.length() > 0;
  if (!ssidValid) {
    Serial.println("Undefined SSID!");
    wifiState.connectionSuccess = false;
    return wifiState.connectionSuccess;
  }
  // Ellenőrizzük az SSID-t, IP-t, gateway-t
  bool ipValid = localIP.fromString(ip.c_str());
  bool gatewayValid = localGateway.fromString(gateway.c_str());
  if (!ipValid) {
    Serial.println("❌ Invalid IP format!");
  }

  if (!gatewayValid) {
    Serial.println("❌ Invalid gateway format!");
  }

  // Csak akkor konfiguráljuk kézzel, ha minden formátum jó
  if (ssidValid && ipValid && gatewayValid) {
    if (!WiFi.config(localIP, localGateway, subnet)) {
      Serial.println("⚠️ STA Failed to configure");
    } else {
      Serial.println("✅ Manual IP config applied.");
    }
  } else {
    Serial.println("➡️ Skipping manual IP config. Using DHCP...");
  }

  // Mindig próbálunk csatlakozni a WiFi-hez
  printUptime();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.println("Connecting to WiFi...");
  Serial.print("Trying to connect to SSID: ");
  Serial.println(ssid);
  blockingDelay(100);
  timing.wifiInitCurrent = millis();
  timing.wifiInitPrev = timing.wifiInitCurrent;
  while (WiFi.status() != WL_CONNECTED) {
    resetbutton();
    wifiresetbutton();
    yield();
    timing.wifiInitCurrent = millis();
    if (timing.wifiInitCurrent - timing.wifiInitPrev >= interval) {
      printUptime();
      Serial.println("Failed to connect.");
      wifiState.connectionSuccess = false;
      return wifiState.connectionSuccess;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    printUptime();
    Serial.print("Connected to: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal strength (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    wifiState.connectionSuccess = true;
    return wifiState.connectionSuccess;
  }

  wifiState.connectionSuccess = false;
  return wifiState.connectionSuccess;
}

bool reset_device() {
  // keep track of number of resets
  timing.resetDeviceCurrent = millis();
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
    testState.resetPulseActive = true;
    testState.resetStep++;
    timing.resetPulseStart = millis();
  }
  if (testState.resetPulseActive && (timing.resetDeviceCurrent - timing.resetPulseStart >= RESET_PULSE)) {
    Serial.println("Reset_pulse delay end.");
    Serial.print("Powering ON the router. Instance = ");
    Serial.println(testState.resetEvents);
    digitalWrite(relayPin, LOW);
    digitalWrite(ledPin, HIGH);
    printUptime();
    testState.resetPulseActive = false;
    testState.resetStep = 0;
    return true;
  }
  return false;
}

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
  Serial.end();
  blockingDelay(500);
  esp_deep_sleep_start();
}

void printUptime() {
  int64_t totalSec = esp_timer_get_time() / 1000000;
  int64_t d = totalSec / 86400;
  int h = (totalSec % 86400) / 3600;
  int m = (totalSec % 3600) / 60;
  int s = totalSec % 60;

  char buf[40];
  if (d > 0) {
    snprintf(buf, sizeof(buf), "Uptime: %lldd %dh %dm %ds", (long long)d, h, m, s);
  } else {
    snprintf(buf, sizeof(buf), "Uptime: %dh %dm %ds", h, m, s);
  }
  Serial.println(buf);
}

void resetbutton() {
  wifiState.buttonStateReset = digitalRead(resetPin);
  if (wifiState.buttonStateReset == LOW) {
    timing.resetBtnCurrent = millis();
    if ((timing.resetBtnCurrent - timing.resetBtnDebounce) > debounceDelay_resetPin) {
      timing.resetBtnDebounce = timing.resetBtnCurrent;
      Serial.println("Reset button pressed.");
      Serial.println("RESTART ESP32C3 device.");
      blockingDelay(500);
      ESP.restart();
    }
  }
}

void wifiresetbutton() {
  wifiState.buttonStateWifiReset = digitalRead(wifiresetPin);
  if (wifiState.buttonStateWifiReset == LOW) {
    timing.wifiResetBtnCurrent = millis();
    if ((timing.wifiResetBtnCurrent - timing.wifiResetBtnDebounce) > debounceDelay_wifiresetPin) {
      timing.wifiResetBtnDebounce = timing.wifiResetBtnCurrent;
      Serial.println("WIFIRESET button is pulling down!");
      Serial.println("RESET saved wifi data!");
      clearFile(LittleFS, gatewayPath);
      clearFile(LittleFS, ipPath);
      clearFile(LittleFS, passPath);
      clearFile(LittleFS, ssidPath);
      Serial.println("RESTART ESP32C3 device.");
      blockingDelay(500);
      ESP.restart();
    }
  }
}

bool reconnectWifi() {

  wifiState.connected = false;
  timing.reconnectStart = millis();
  wifiState.attempts = 0;
  printUptime();
  Serial.println("Starting reconnectWifi loop");

  while (wifiState.attempts < wifi_maxRetries) {

    timing.reconnectCurrent = millis();

    if (wifiState.attempts == 0 || (timing.reconnectCurrent - timing.reconnectStart >= wifiInterval)) {
      timing.reconnectStart = timing.reconnectCurrent;
      if (!uiFlags.wifiAttemptPrinted) {
        printUptime();
        Serial.print("Attempt ");
        Serial.println(wifiState.attempts + 1);
        uiFlags.wifiAttemptPrinted = true;
      }
      Serial.println("Calling initWiFi()");
      if (initWiFi()) {
        printUptime();
        Serial.println("WIFI RECONECTED! In FAILURE_STATE.");
        digitalWrite(wifiledPin, HIGH);
        uiFlags.wifiAttemptPrinted = false;
        Serial.println("Exiting loop with wifiConnected = true");
        wifiState.connected = true;
        return wifiState.connected;

      } else {
        wifiState.connected = false;
        printUptime();
        Serial.println("WIFI ERROR! In FAILURE_STATE.");
        if (wifiState.attempts < wifi_maxRetries - 1) {
          printUptime();
          Serial.print(wifiInterval / 1000);
          Serial.println(" seconds delay start.");
        }
        Serial.print("WiFi status: ");
        Serial.println(WiFi.status());
        timing.reconnectStart = millis();
        wifiState.attempts++;
        uiFlags.wifiAttemptPrinted = false;
      }
    }
  }

  if (!wifiState.connected) {
    printUptime();
    Serial.print("WIFI FAILED TO RECONNECT AFTER ");
    Serial.print(wifi_maxRetries);
    Serial.println(" wifi_ATTEMPTS!");
    tosleep();
    wifiState.connected = false;  //ha gond lenne a tosleep-el
    return wifiState.connected;
  }
  return wifiState.connected;
}

bool testInternetHTTP(const char* url, const char* expectedResponse) {
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(url);
  int httpCode = http.GET();
  bool result = false;

  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println(payload);
    result = payload.equals(expectedResponse);
    Serial.println(result ? "Igaz érték!" : "Hamis érték!");
  } else {
    Serial.print("Error on HTTP request, code: ");
    Serial.println(httpCode);
  }
  http.end();
  return result;
}

bool testInternet3() {
  Serial.println("Ping teszt futtatása (Cloudflare - 1.1.1.1)...");
  IPAddress remote_ip(1, 1, 1, 1);  // Cloudflare DNS IP-je
  int successCount = 0;

  for (int j = 0; j < 4; j++) {
    bool pingOK = Ping.ping(remote_ip, 1);  // 1 próbálkozás pingenként
    if (pingOK) {
      Serial.print("Ping ");
      Serial.print(j + 1);
      Serial.println(" sikeres.");
      successCount++;
    } else {
      Serial.print("Ping ");
      Serial.print(j + 1);
      Serial.println(" sikertelen.");
      if (j == 0) {
        Serial.println("⚠️ Első ping hiba — lehet, hogy a hálózat ébred.");
      }
    }
    blockingDelay(1000);  // Kíméletes tesztelés
  }

  if (successCount < 2) {
    Serial.println("❌ Ping teszt sikertelen — hálózati probléma valószínű.");
    return false;
  } else {
    Serial.println("✅ Ping teszt sikeres.");
    return true;
  }
}

void handleFirstStart(unsigned long currentMillis) {
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

    while (!reconnectWifi()) {
      resetbutton();
      wifiresetbutton();
      yield();
    }

    timing.startMillis = currentMillis;  // újraindítjuk az időzítést
  }
  uiFlags.firstStart = false;
}

void setup() {
  timing.startMillis = millis();

  pinMode(wifiresetPin, INPUT_PULLUP);
  pinMode(resetPin, INPUT_PULLUP);
  esp_sleep_enable_timer_wakeup(3600000000);  // Wake up after 1 hour
  // esp_deep_sleep_enable_gpio_wakeup(BIT(D1), ESP_GPIO_WAKEUP_GPIO_LOW); // ha gombbal szeretném felébreszteni akkor ezeket a sorokat törölni kell: esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL) esp_sleep_enable_timer_wakeup(60 * 60 * 1000000)
  pinMode(ledPin, OUTPUT);
  pinMode(wifiledPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  digitalWrite(wifiledPin, LOW);
  digitalWrite(relayPin, LOW);
  digitalWrite(ledPin, HIGH);  //bekapcsolja a ledet, +5volt 150 ohm
  Serial.begin(115200);
  unsigned long serialTimeout = millis();
  while (!Serial && millis() - serialTimeout < 3000) { yield(); }
  blockingDelay(500);

  printUptime();

  if (digitalRead(resetPin) == LOW) {
    handleStuckButton("Reset button got stuck.");
  }
  if (digitalRead(wifiresetPin) == LOW) {
    handleStuckButton("Wifireset button got stuck.");
  }

  Serial.println("Init LittleFS.");
  initLittleFS();
  // Load values saved in LittleFS
  ssid = readFile(LittleFS, ssidPath);
  pass = readFile(LittleFS, passPath);
  ip = readFile(LittleFS, ipPath);
  gateway = readFile(LittleFS, gatewayPath);

  Serial.println(ssid);
  Serial.println(String(pass.length()) + " chars password loaded");
  Serial.println(ip);
  Serial.println(gateway);

  if (initWiFi()) {
    Serial.println("WIFI OK!");
    digitalWrite(wifiledPin, HIGH);  //led on
    blockingDelay(100);
  } else {
    digitalWrite(wifiledPin, LOW);  //led off
    blockingDelay(100);
    // Connect to Wi-Fi network with SSID and password
    Serial.println("Setting AP (Access Point)");
    String apName = "ESP-" + String(ESP.getChipModel());
    WiFi.softAP(apName.c_str(), AP_PASSWORD);
    Serial.print("AP password: ");
    Serial.println(AP_PASSWORD);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
      request->send(LittleFS, "/wifimanager.html", "text/html");
    });

    server.serveStatic("/", LittleFS, "/");

    server.on("/", HTTP_POST, [](AsyncWebServerRequest* request) {
      int params = request->params();
      for (int i = 0; i < params; i++) {
        const AsyncWebParameter* p = request->getParam(i);
        if (p->isPost()) {
          // HTTP POST ssid value
          if (p->name() == PARAM_SSID) {
            String val = p->value().c_str();
            if (val.length() > 0 && val.length() <= 32) {
              ssid = val;
              Serial.print("SSID set to: ");
              Serial.println(ssid);
              writeFile(LittleFS, ssidPath, ssid.c_str());
            } else {
              Serial.println("Invalid SSID length!");
            }
          }
          // HTTP POST pass value
          if (p->name() == PARAM_PASS) {
            String val = p->value().c_str();
            if (val.length() <= 63) {
              pass = val;
              Serial.print("Password set to: ");
              Serial.println(String(pass.length()) + " chars");
              writeFile(LittleFS, passPath, pass.c_str());
            } else {
              Serial.println("Password too long!");
            }
          }
          // HTTP POST ip value
          if (p->name() == PARAM_IP) {
            IPAddress testIP;
            String val = p->value().c_str();
            if (testIP.fromString(val.c_str())) {
              ip = val;
              Serial.print("IP Address set to: ");
              Serial.println(ip);
              writeFile(LittleFS, ipPath, ip.c_str());
            } else {
              Serial.println("Invalid IP format!");
            }
          }
          // HTTP POST gateway value
          if (p->name() == PARAM_GATEWAY) {
            IPAddress testIP;
            String val = p->value().c_str();
            if (testIP.fromString(val.c_str())) {
              gateway = val;
              Serial.print("Gateway set to: ");
              Serial.println(gateway);
              writeFile(LittleFS, gatewayPath, gateway.c_str());
            } else {
              Serial.println("Invalid gateway format!");
            }
          }
          Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
        }
      }
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
      Serial.println("RESTART!");
      blockingDelay(2000);
      ESP.restart();
    });
    server.begin();
  }
}

void loop() {

  unsigned long currentMillis = millis();

  if (uiFlags.firstStart) {
    handleFirstStart(currentMillis);
    return;  // így biztosan nem fut le semmi más ebben a körben
  }

  resetbutton();
  wifiresetbutton();

  timing.currentLoop = millis();

  switch (currentState) {

    case TESTING_STATE: {
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected before test!");
        digitalWrite(wifiledPin, LOW);
        currentState = FAILURE_STATE;
        timing.stateStart = timing.currentLoop;
        testState.failedCount++;
        break;
      }
      printUptime();
      Serial.println("Beginning Test.");
      Serial.print("Teszt ciklus index = ");
      Serial.print(testState.cycleIndex);
      Serial.print(" | Hibák száma = ");
      Serial.println(testState.failedCount);

      bool testResult;
      if (testState.cycleIndex == 1 || testState.cycleIndex == 3) {
        testResult = testInternet3();
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
      timing.stateStart = timing.currentLoop;
      break;
    }

    case FAILURE_STATE:
      if (testState.cycleIndex > 3 && testState.failedCount >= 3) {

        if (!uiFlags.resetPrinted) {
          printUptime();
          Serial.println("Begining Reset in FAILURE_STATE.");
          while (!reset_device()) {
            resetbutton();
            wifiresetbutton();
          }
          printUptime();
          Serial.println("Reset is done in FAILURE_STATE.");
          Serial.println("RESET_DELAY start in FAILURE_STATE.");
          timing.currentLoop = millis();
          timing.stateStart = timing.currentLoop;
          uiFlags.resetPrinted = true;  // Set the flag after printing
        }

        if (timing.currentLoop - timing.stateStart >= RESET_DELAY) {
          printUptime();
          Serial.println("RESET_DELAY end in FAILURE_STATE.");
          Serial.println("Reconnect WIFI in FAILURE_STATE.");
          WiFi.disconnect(true);
          blockingDelay(100);
          WiFi.begin(ssid.c_str(), pass.c_str());
          unsigned long reconnect_countdown = millis();
          while (millis() - reconnect_countdown < interval) {
            yield();  // Wait without blocking other processes
          }
          if (WiFi.status() != WL_CONNECTED) {
            printUptime();
            Serial.println("WIFI fail, Restart WIFI in FAILURE_STATE.");
            digitalWrite(wifiledPin, LOW);

            while (!reconnectWifi()) {
              resetbutton();
              wifiresetbutton();
              yield();
            }

          } else {
            printUptime();
            Serial.println("WIFI OK in FAILURE_STATE.");
            digitalWrite(wifiledPin, HIGH);
          }

          testState.cycleIndex = 0;
          testState.failedCount = 0;
          uiFlags.resetPrinted = false;
          currentState = TESTING_STATE;
        }

      } else {
        if (timing.currentLoop - timing.stateStart >= PROBE_DELAY) {
          if (testState.cycleIndex < 10) testState.cycleIndex++;
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

      if (timing.currentLoop - timing.stateStart >= SUCCESS_DELAY) {
        printUptime();
        Serial.println("SUCCESS_DELAY delay end.");
        uiFlags.successPrinted = false;
        timing.stateStart = timing.currentLoop;
        currentState = TESTING_STATE;
      }
      break;
  }
}
