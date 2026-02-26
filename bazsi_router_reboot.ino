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
const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";
const char* PARAM_INPUT_3 = "ip";
const char* PARAM_INPUT_4 = "gateway";
int res;

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
IPAddress subnet(255, 255, 0, 0);

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
uint8_t Nreset_events = 0;
const uint8_t wifi_maxRetries = 3;
const uint32_t wifiInterval = 20 * 1000;
uint8_t i = 0;
int failedTestsCount = 0;

unsigned long previousMillis_initWiFi = 0;
unsigned long currentMillis_initWiFi = 0;
unsigned long stateStartMillis_loop = 0;
unsigned long currentMillis_loop = 0;
unsigned long previousMillisResetPulse_reset_device = 0;
unsigned long currentMillis_reconnectWifi = 0;
unsigned long currentMillis_reset_device = 0;
unsigned long startAttemptTime_reconnectWifi = 0;
unsigned long lastDebounceTime_resetPin = 0;
const unsigned long debounceDelay_resetPin = 50;
unsigned long lastDebounceTime_wifiresetPin = 0;
const unsigned long debounceDelay_wifiresetPin = 50;
unsigned long currentMillis_wifiresetbutton = 0;
unsigned long currentMillis_resetbutton = 0;
unsigned long startMillis = millis();
bool firstStart = true;
int buttonState_resetPin = HIGH;
int buttonState_wifiresetPin = HIGH;

bool successfulTestPrinted = false;  // Flag to track if "Successful Test" has been printed
bool beginResetPrinted = false;      // Flag to track if "Begining Reset" has been printed
bool wifiConnected = false;
bool firstStartPrinted = false;
bool printAttempts = false;
bool connectionSuccess = false;  // Állapotjelző változó
int wifi_attempts = 0;

bool resetPulseActive = false;
uint8_t step_reset_device = 0;

enum {
  TESTING_STATE = 0,
  FAILURE_STATE = 1,
  SUCCESS_STATE = 2
};

int CurrentState = TESTING_STATE;

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
  file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to reopen file for clearing");
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
  if (file.print("")) { Serial.println("- file content erased"); }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- write failed or empty file");
  }
  file.close();
}

void nonBlockingDelay(unsigned long duration) {
  unsigned long start = millis();
  while (millis() - start < duration) {
    yield();
  }
}

void handleStuckButton(const char* message) {
  Serial.println(message);
  digitalWrite(ledPin, LOW);
  esp_sleep_enable_timer_wakeup(500 * 1000);
  esp_deep_sleep_start();
}

// Initialize WiFi
bool initWiFi() {

  printUptime();
  bool ssidValid = ssid.length() > 0;
  if (!ssidValid) {
    Serial.println("Undefined SSID!");
    connectionSuccess = false;
    return connectionSuccess;
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
  nonBlockingDelay(100);
  currentMillis_initWiFi = millis();
  previousMillis_initWiFi = currentMillis_initWiFi;
  while (WiFi.status() != WL_CONNECTED) {
    resetbutton();
    wifiresetbutton();
    yield();
    currentMillis_initWiFi = millis();
    if (currentMillis_initWiFi - previousMillis_initWiFi >= interval) {
      printUptime();
      Serial.println("Failed to connect.");
      connectionSuccess = false;
      return connectionSuccess;
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
    connectionSuccess = true;
    return connectionSuccess;
  }

  connectionSuccess = false;
  return connectionSuccess;
}

bool reset_device() {
  // keep track of number of resets
  currentMillis_reset_device = millis();
  if (step_reset_device == 0) {
    Nreset_events++;
    if (Nreset_events == maxfailureEvents) {
      tosleep();
    }
    Serial.println("Router resetting");
    Serial.println(String("Powering OFF the router. Instance = ") + String(Nreset_events));
    digitalWrite(relayPin, HIGH);
    Serial.println("Relay on.");
    digitalWrite(ledPin, LOW);
    Serial.println("Reset_pulse delay.");
    resetPulseActive = true;
    step_reset_device++;
    previousMillisResetPulse_reset_device = millis();
  }
  if (resetPulseActive && (currentMillis_reset_device - previousMillisResetPulse_reset_device >= RESET_PULSE)) {
    Serial.println("Reset_pulse delay end.");
    Serial.println(String("Powering ON the router. Instance = ") + String(Nreset_events));
    digitalWrite(relayPin, LOW);
    digitalWrite(ledPin, HIGH);
    printUptime();
    resetPulseActive = false;
    step_reset_device = 0;
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
  nonBlockingDelay(500);
  esp_deep_sleep_start();
}

void printUptime() {
  int64_t timeInMicroseconds = esp_timer_get_time();
  int64_t timeInSeconds = timeInMicroseconds / 1000000;

  int64_t months = timeInSeconds / 2592000;  // kb. 30 napos hónap
  int64_t days = (timeInSeconds % 2592000) / 86400;
  int64_t hours = (timeInSeconds % 86400) / 3600;
  int64_t minutes = (timeInSeconds % 3600) / 60;
  int64_t seconds = timeInSeconds % 60;

  String uptimeStr = "Uptime: ";
  if (months > 0) uptimeStr += String(months) + "m ";
  if (days > 0 || months > 0) uptimeStr += String(days) + "d ";
  uptimeStr += String(hours) + "h ";
  uptimeStr += String(minutes) + "m ";
  uptimeStr += String(seconds) + "s";

  Serial.println(uptimeStr);
}

void resetbutton() {
  buttonState_resetPin = digitalRead(resetPin);
  if (buttonState_resetPin == LOW) {
    currentMillis_resetbutton = millis();
    if ((currentMillis_resetbutton - lastDebounceTime_resetPin) > debounceDelay_resetPin) {
      lastDebounceTime_resetPin = currentMillis_resetbutton;
      Serial.println("Reset button pressed.");
      Serial.println("RESTART ESP32C3 device.");
      nonBlockingDelay(500);
      ESP.restart();
    }
  }
}

void wifiresetbutton() {
  buttonState_wifiresetPin = digitalRead(wifiresetPin);
  if (buttonState_wifiresetPin == LOW) {
    currentMillis_wifiresetbutton = millis();
    if ((currentMillis_wifiresetbutton - lastDebounceTime_wifiresetPin) > debounceDelay_wifiresetPin) {
      lastDebounceTime_wifiresetPin = currentMillis_wifiresetbutton;
      Serial.println("WIFIRESET button is pulling down!");
      Serial.println("RESET saved wifi data!");
      clearFile(LittleFS, gatewayPath);
      clearFile(LittleFS, ipPath);
      clearFile(LittleFS, passPath);
      clearFile(LittleFS, ssidPath);
      Serial.println("RESTART ESP32C3 device.");
      nonBlockingDelay(500);
      ESP.restart();
    }
  }
}

bool reconnectWifi() {

  ssid = readFile(LittleFS, ssidPath);
  pass = readFile(LittleFS, passPath);
  ip = readFile(LittleFS, ipPath);
  gateway = readFile(LittleFS, gatewayPath);

  wifiConnected = false;
  startAttemptTime_reconnectWifi = millis();
  wifi_attempts = 0;
  printUptime();
  Serial.println("Starting reconnectWifi loop");

  while (wifi_attempts < wifi_maxRetries) {

    currentMillis_reconnectWifi = millis();

    if (wifi_attempts == 0 || (currentMillis_reconnectWifi - startAttemptTime_reconnectWifi >= wifiInterval)) {
      startAttemptTime_reconnectWifi = currentMillis_reconnectWifi;
      if (!printAttempts) {
        printUptime();
        Serial.print("Attempt ");
        Serial.println(wifi_attempts + 1);
        printAttempts = true;
      }
      Serial.println("Calling initWiFi()");
      if (initWiFi()) {
        printUptime();
        Serial.println("WIFI RECONECTED! In FAILURE_STATE.");
        digitalWrite(wifiledPin, HIGH);
        printAttempts = false;
        Serial.println("Exiting loop with wifiConnected = true");
        wifiConnected = true;
        return wifiConnected;

      } else {
        wifiConnected = false;
        printUptime();
        Serial.println("WIFI ERROR! In FAILURE_STATE.");
        if (wifi_attempts < wifi_maxRetries - 1) {
          printUptime();
          Serial.print(wifiInterval / 1000);
          Serial.println(" seconds delay start.");
        }
        Serial.print("WiFi status: ");
        Serial.println(WiFi.status());
        startAttemptTime_reconnectWifi = millis();
        wifi_attempts++;
        printAttempts = false;
      }
    }
  }

  if (!wifiConnected) {
    printUptime();
    Serial.print("WIFI FAILED TO RECONNECT AFTER ");
    Serial.print(wifi_maxRetries);
    Serial.println(" wifi_ATTEMPTS!");
    tosleep();
    wifiConnected = false;  //ha gond lenne a tosleep-el
    return wifiConnected;
  }
  return wifiConnected;
}

bool testInternet1() {

  HTTPClient http;

  http.begin("http://www.msftncsi.com/ncsi.txt");
  int httpCode = http.GET();

  if (httpCode > 0) {  // Check for the returning code

    String payload = http.getString();
    Serial.println(payload);

    if (payload.equals("Microsoft NCSI")) {
      Serial.println("Igaz érték!");
      http.end();
      return true;
    } else {
      Serial.println("Hamis érték!");
      http.end();
      return false;
    }
  } else {
    Serial.println("Error on HTTP request");
    http.end();
    return false;
  }

  http.end();  //Free the resources

  return false;
}

bool testInternet2() {

  HTTPClient http;

  http.begin("http://www.msftconnecttest.com/connecttest.txt");
  int httpCode = http.GET();

  if (httpCode > 0) {  // Check for the returning code

    String payload = http.getString();
    Serial.println(payload);

    if (payload.equals("Microsoft Connect Test")) {
      Serial.println("Igaz érték!");
      http.end();
      return true;
    } else {
      Serial.println("Hamis érték!");
      http.end();
      return false;
    }
  } else {
    Serial.println("Error on HTTP request");
    http.end();
    return false;
  }

  http.end();  //Free the resources

  return false;
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
    nonBlockingDelay(1000);  // Kíméletes tesztelés
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
    if (currentMillis - startMillis < firstStartDelay) {
      if (!firstStartPrinted) {
        printUptime();
        Serial.println("First start wait begin.");
        firstStartPrinted = true;
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

    startMillis = currentMillis;  // újraindítjuk az időzítést
  }
  firstStart = false;
}

void setup() {

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
  while (!Serial) {};
  nonBlockingDelay(500);

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
  Serial.println(pass);
  Serial.println(ip);
  Serial.println(gateway);

  if (initWiFi()) {
    Serial.println("WIFI OK!");
    digitalWrite(wifiledPin, HIGH);  //led on
    server.end();
    nonBlockingDelay(100);
  } else {
    digitalWrite(wifiledPin, LOW);  //led off
    nonBlockingDelay(100);
    // Connect to Wi-Fi network with SSID and password
    Serial.println("Setting AP (Access Point)");
    // NULL sets an open Access Point
    String apName = "ESP-" + String(ESP.getChipModel());
    WiFi.softAP(apName.c_str(), NULL);
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
          if (p->name() == PARAM_INPUT_1) {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            // Write file to save value
            writeFile(LittleFS, ssidPath, ssid.c_str());
          }
          // HTTP POST pass value
          if (p->name() == PARAM_INPUT_2) {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            // Write file to save value
            writeFile(LittleFS, passPath, pass.c_str());
          }
          // HTTP POST ip value
          if (p->name() == PARAM_INPUT_3) {
            ip = p->value().c_str();
            Serial.print("IP Address set to: ");
            Serial.println(ip);
            // Write file to save value
            writeFile(LittleFS, ipPath, ip.c_str());
          }
          // HTTP POST gateway value
          if (p->name() == PARAM_INPUT_4) {
            gateway = p->value().c_str();
            Serial.print("Gateway set to: ");
            Serial.println(gateway);
            // Write file to save value
            writeFile(LittleFS, gatewayPath, gateway.c_str());
          }
          Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
        }
      }
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
      Serial.println("RESTART!");
      nonBlockingDelay(2000);
      ESP.restart();
    });
    server.begin();
  }
}

void loop() {

  unsigned long currentMillis = millis();

  if (firstStart) {
    handleFirstStart(currentMillis);
    return;  // így biztosan nem fut le semmi más ebben a körben
  }

  resetbutton();
  wifiresetbutton();

  currentMillis_loop = millis();

  switch (CurrentState) {

    case TESTING_STATE:

      printUptime();
      Serial.println("Begining Test.");
      Serial.print("Teszt ciklus index = ");
      Serial.print(i);
      Serial.print(" | Hibák száma = ");
      Serial.println(failedTestsCount);

      if (i == 1 || i == 3) {
        if (!testInternet3()) {
          failedTestsCount++;
          printUptime();
          Serial.println("Failed testInternet3.");
          CurrentState = FAILURE_STATE;
          stateStartMillis_loop = currentMillis_loop;
        } else {
          i = 0;
          failedTestsCount = 0;
          Nreset_events = 0;
          CurrentState = SUCCESS_STATE;
          stateStartMillis_loop = currentMillis_loop;
        }
      } else if (i == 2 || i == 4) {
        if (!testInternet1()) {
          failedTestsCount++;
          printUptime();
          Serial.println("Failed testInternet1.");
          CurrentState = FAILURE_STATE;
          stateStartMillis_loop = currentMillis_loop;
        } else {
          i = 0;
          failedTestsCount = 0;
          Nreset_events = 0;
          CurrentState = SUCCESS_STATE;
          stateStartMillis_loop = currentMillis_loop;
        }
      } else {
        if (!testInternet2()) {
          failedTestsCount++;
          printUptime();
          Serial.println("Failed testInternet2.");
          CurrentState = FAILURE_STATE;
          stateStartMillis_loop = currentMillis_loop;
        } else {
          i = 0;
          failedTestsCount = 0;
          Nreset_events = 0;
          CurrentState = SUCCESS_STATE;
          stateStartMillis_loop = currentMillis_loop;
        }
      }
      break;

    case FAILURE_STATE:
      if (i > 3 && failedTestsCount >= 3) {

        if (!beginResetPrinted) {
          printUptime();
          Serial.println("Begining Reset in FAILURE_STATE.");
          while (!reset_device()) {
            resetbutton();
            wifiresetbutton();
          }
          printUptime();
          Serial.println("Reset is done in FAILURE_STATE.");
          Serial.println("RESET_DELAY start in FAILURE_STATE.");
          currentMillis_loop = millis();
          stateStartMillis_loop = currentMillis_loop;
          beginResetPrinted = true;  // Set the flag after printing
        }

        if (currentMillis_loop - stateStartMillis_loop >= RESET_DELAY) {
          printUptime();
          Serial.println("RESET_DELAY end in FAILURE_STATE.");
          Serial.println("Reconnect WIFI in FAILURE_STATE.");
          WiFi.disconnect();
          WiFi.reconnect();
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

          i = 0;
          failedTestsCount = 0;
          beginResetPrinted = false;
          CurrentState = TESTING_STATE;
        }

      } else {
        if (currentMillis_loop - stateStartMillis_loop >= PROBE_DELAY) {
          i = i + 1;
          CurrentState = TESTING_STATE;
        }
      }
      break;

    case SUCCESS_STATE:

      if (!successfulTestPrinted) {
        printUptime();
        Serial.println("Successful Test");
        Serial.println();
        Serial.println("SUCCESS_DELAY delay start.");
        successfulTestPrinted = true;  // Set the flag to true after printing
      }

      if (currentMillis_loop - stateStartMillis_loop >= SUCCESS_DELAY) {
        printUptime();
        Serial.println("SUCCESS_DELAY delay end.");
        successfulTestPrinted = false;
        stateStartMillis_loop = currentMillis_loop;
        CurrentState = TESTING_STATE;
      }
      break;
  }
}