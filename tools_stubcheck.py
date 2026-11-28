#!/usr/bin/env python3
"""A test/stubs modellek igazolasa a valodi konyvtarak forrasa ellen."""
import re, sys, pathlib

GYOKER = pathlib.Path(__file__).resolve().parent
hibak, ellenorzesek, nem_modellezett = [], 0, []

def keres(gyokerek, veg):
    """Az elso olyan fajl, aminek az utja a 'veg'-re vegzodik."""
    talalt = []
    for g in gyokerek:
        talalt += [p for p in pathlib.Path(g).rglob(pathlib.PurePath(veg).name)
                   if str(p).replace('\\', '/').endswith(veg)]
    if not talalt:
        hibak.append(f"NEM TALALHATO a valodi forras: {veg}")
        return None
    return sorted(talalt, key=lambda p: len(str(p)))[0]

def szoveg(p):
    return p.read_text(encoding='utf-8', errors='replace') if p else ''

def allit(mit, valodi, mienk):
    global ellenorzesek
    ellenorzesek += 1
    if valodi is None or mienk is None:
        hibak.append(f"{mit}: NEM SIKERULT KIOLVASNI "
                     f"(valodi={valodi!r}, stub={mienk!r})")
    elif valodi != mienk:
        hibak.append(f"{mit}: a valodi {valodi!r}, a stub {mienk!r}")

def egy(sz, minta, csoport=1):
    m = re.search(minta, sz, re.M)
    return m.group(csoport) if m else None

def main():
    gyokerek = sys.argv[1:]
    if not gyokerek:
        print("hasznalat: tools_stubcheck.py <gyoker> [<gyoker> ...]")
        return 2

    stub_wifi   = szoveg(GYOKER / 'test/stubs/WiFi.h')
    stub_sys    = szoveg(GYOKER / 'test/stubs/esp_system.h')
    stub_wdt    = szoveg(GYOKER / 'test/stubs/esp_task_wdt.h')
    stub_http   = szoveg(GYOKER / 'test/stubs/HTTPClient.h')
    stub_aws    = szoveg(GYOKER / 'test/stubs/ESPAsyncWebServer.h')
    stub_ping   = szoveg(GYOKER / 'test/stubs/ESPping.h')
    stub_ard    = szoveg(GYOKER / 'test/stubs/Arduino.h')
    teszt       = szoveg(GYOKER / 'test/test_main.cpp')

    pins = keres(gyokerek, 'variants/XIAO_ESP32C3/pins_arduino.h')
    ps = szoveg(pins)
    LAB = {'D10': 'PIN_RELAY', 'D4': 'PIN_LED', 'D3': 'PIN_WIFILED',
           'D1': 'PIN_RESETBTN', 'D0': 'PIN_WIFIBTN'}
    for d, konst in LAB.items():
        valodi = egy(ps, r'^static const uint8_t %s\s*=\s*(\d+)\s*;' % d)
        mienk = egy(teszt, r'static const int %s\s*=\s*(\d+)\s*;' % konst)
        allit(f"labkiosztas {d} ({konst})", valodi, mienk)

    wt = szoveg(keres(gyokerek, 'libraries/WiFi/src/WiFiType.h'))
    for nev in ['WL_IDLE_STATUS', 'WL_NO_SSID_AVAIL', 'WL_CONNECTED',
                'WL_CONNECT_FAILED', 'WL_DISCONNECTED', 'WL_SCAN_COMPLETED',
                'WL_CONNECTION_LOST']:
        m = egy(stub_wifi, r'^#define %s (\d+)' % nev)
        if m is None:
            nem_modellezett.append(f"wl_status_t {nev}")
            continue
        allit(f"wl_status_t {nev}", egy(wt, r'^\s*%s\s*=\s*(\d+)' % nev), m)

    es = szoveg(keres(gyokerek, 'esp_system.h'))
    valodi_sorrend = re.findall(r'\bESP_RST_[A-Z_]+', es)
    stub_sorrend = re.findall(r'\bESP_RST_[A-Z_]+', stub_sys)
    n = min(len(valodi_sorrend), len(stub_sorrend))
    if n == 0:
        hibak.append("esp_reset_reason_t: egyik oldalrol sem sikerult kiolvasni")
    for i in range(n):
        allit(f"esp_reset_reason_t[{i}]", valodi_sorrend[i], stub_sorrend[i])

    tw = szoveg(keres(gyokerek, 'esp_system/include/esp_task_wdt.h'))
    blokk = re.search(r'typedef struct \{(.*?)\} esp_task_wdt_config_t;', tw, re.S)
    sblokk = re.search(r'typedef struct \{(.*?)\} esp_task_wdt_config_t;', stub_wdt, re.S)
    if not blokk or not sblokk:
        hibak.append("esp_task_wdt_config_t: a struktura nem talalhato")
    else:
        vm = re.findall(r'\b(uint32_t|bool|int)\s+(\w+)\s*;', blokk.group(1))
        sm = re.findall(r'\b(uint32_t|bool|int)\s+(\w+)\s*;', sblokk.group(1))
        allit("esp_task_wdt_config_t mezoi", vm, sm)

    ee = szoveg(keres(gyokerek, 'esp_common/include/esp_err.h'))
    for nev in ['ESP_OK', 'ESP_FAIL', 'ESP_ERR_NO_MEM', 'ESP_ERR_INVALID_ARG',
                'ESP_ERR_INVALID_STATE', 'ESP_ERR_NOT_FOUND']:
        v = egy(ee, r'^#define %s\s+(-?\w+)' % nev)
        m = egy(stub_wdt, r'^#define %s (-?\w+)' % nev)
        if m is None:
            nem_modellezett.append(f"esp_err kod {nev}")
            continue
        allit(f"esp_err kod {nev}", v, m)

    hc = szoveg(keres(gyokerek, 'libraries/HTTPClient/src/HTTPClient.h'))
    for fn in ['setConnectTimeout', 'setTimeout']:
        allit(f"HTTPClient::{fn} parametertipusa",
              egy(hc, r'void %s\((\w+)\s' % fn),
              egy(stub_http, r'void %s\((\w+)\)' % fn))

    aw = szoveg(keres(gyokerek, 'src/ESPAsyncWebServer.h'))
    allit("AsyncWebServerRequest::params() visszateresi tipusa",
          egy(aw, r'(\w+) params\(\) const'),
          egy(stub_aws, r'(\w+) params\(\) const'))
    allit("AsyncWebParameter::name() visszateresi tipusa",
          'const String &' if re.search(r'const String &name\(\) const', aw) else None,
          'const String &' if re.search(r'const String& name\(\) const', stub_aws) else None)
    allit("AsyncWebParameter::value() visszateresi tipusa",
          'const String &' if re.search(r'const String &value\(\) const', aw) else None,
          'const String &' if re.search(r'const String& value\(\) const', stub_aws) else None)
    allit("AsyncWebParameter::isPost() letezik",
          bool(re.search(r'bool isPost\(\) const', aw)),
          bool(re.search(r'bool isPost\(\) const', stub_aws)))
    allit("getParam(size_t) letezik",
          bool(re.search(r'getParam\(size_t', aw)),
          bool(re.search(r'getParam\(size_t', stub_aws)))

    sta = szoveg(keres(gyokerek, 'libraries/WiFi/src/STA.cpp'))
    allit("STA.cpp: a 'first_connect' feltetel az AUTH_FAIL agon",
          bool(re.search(r'WIFI_REASON_AUTH_FAIL\)\s*&&\s*!first_connect', sta)),
          True)
    mc = szoveg(keres(gyokerek, 'cores/esp32/main.cpp'))
    allit("main.cpp: a loopTask feltetelesen etet minden iteracioban",
          bool(re.search(r'if \(loopTaskWDTEnabled\)\s*\{\s*esp_task_wdt_reset\(\);', mc)),
          True)

    wg = szoveg(keres(gyokerek, 'libraries/WiFi/src/WiFiGeneric.h'))
    allit("WiFiGenericClass::setSleep(bool) visszateresi tipusa",
          egy(wg, r'(\w+) setSleep\(bool enabled\);'),
          egy(stub_wifi, r'(\w+) setSleep\(bool enabled\)'))
    allit("WiFiSTAClass::gatewayIP() visszateresi tipusa",
          egy(szoveg(keres(gyokerek, 'libraries/WiFi/src/WiFiSTA.h')), r'(IPAddress) gatewayIP\(\);'),
          egy(stub_wifi, r'(IPAddress) gatewayIP\(\)'))
    nic = szoveg(keres(gyokerek, 'libraries/Network/src/NetworkInterface.cpp'))
    allit("NetworkInterface::gatewayIP(): netif nelkul 0.0.0.0, kulonben a netif gw-je",
          bool(re.search(r'IPAddress NetworkInterface::gatewayIP\(\) const \{\s*if \(_esp_netif == NULL\) \{\s*return IPAddress\(\);', nic))
          and 'return IPAddress(ip.gw.addr);' in nic, True)
    ni = szoveg(keres(gyokerek, 'libraries/Network/src/NetworkInterface.h'))
    allit("NetworkInterface::dnsIP(uint8_t, IPAddress) setter",
          egy(ni, r'(\w+) dnsIP\(uint8_t dns_no, IPAddress ip\);'),
          egy(stub_wifi, r'(\w+) dnsIP\(uint8_t dns_no, IPAddress ip\)'))
    ws = szoveg(keres(gyokerek, 'libraries/WiFi/src/WiFiSTA.h'))
    allit("WiFiSTAClass: a STA tag publikus (WiFi.STA.dnsIP)",
          bool(re.search(r'class WiFiSTAClass \{\s*public:\s*STAClass STA;', ws)), True)
    ia = szoveg(keres(gyokerek, 'cores/esp32/IPAddress.h'))
    allit("IPAddress::operator[] const",
          bool(re.search(r'uint8_t operator\[\]\(int index\) const;', ia)),
          bool(re.search(r'uint8_t operator\[\]\(int i\) const', stub_ard)))
    er = szoveg(keres(gyokerek, 'esp_hw_support/include/esp_random.h'))
    allit("esp_random() szignaturaja (az AP jelszo veletlenje)",
          bool(re.search(r'uint32_t esp_random\(void\);', er)),
          bool(re.search(r'uint32_t esp_random\(void\);', szoveg(GYOKER / 'test/stubs/esp_random.h'))))
    allit("esp_random(): bekapcsolt Wi-Fi mellett valodi veletlen (a dokumentalt feltetel)",
          bool(re.search(r'If Wi-Fi or Bluetooth are enabled, this function returns true random', er)), True)
    eh = szoveg(keres(gyokerek, 'src/ESPping.h'))
    allit("ESPping::averageTime() tipusa",
          egy(eh, r'(\w+) averageTime\(\);'),
          egy(stub_ping, r'(\w+) averageTime\(\)'))
    ec = szoveg(keres(gyokerek, 'src/ESPping.cpp'))
    allit("ESPping: _success = _expected_count - _errors (a fantom-siker oka)",
          bool(re.search(r'_success = _expected_count - _errors;', ec)), True)

    stub_udp = szoveg(GYOKER / 'test/stubs/WiFiUdp.h')
    uh = szoveg(keres(gyokerek, 'libraries/Network/src/NetworkUdp.h'))
    uc = szoveg(keres(gyokerek, 'libraries/Network/src/NetworkUdp.cpp'))
    allit("WiFiUdp.h: WiFiUDP = NetworkUDP",
          bool(re.search(r'typedef NetworkUDP WiFiUDP;', szoveg(keres(gyokerek, 'libraries/WiFi/src/WiFiUdp.h')))), True)
    for minta, nev in [(r'uint8_t begin\(uint16_t p\);', 'begin(uint16_t)'),
                       (r'int beginPacket\(IPAddress ip, uint16_t port\);', 'beginPacket(IPAddress, uint16_t)'),
                       (r'int endPacket\(\);', 'endPacket()'),
                       (r'int parsePacket\(\);', 'parsePacket()'),
                       (r'void clear\(\);', 'clear()'),
                       (r'IPAddress remoteIP\(\);', 'remoteIP()'),
                       (r'uint16_t remotePort\(\);', 'remotePort()')]:
        allit(f"NetworkUDP::{nev} letezik", bool(re.search(minta, uh)), True)
    allit("NetworkUDP::flush() elavult (a stub is annak jeloli)",
          bool(re.search(r'\[\[deprecated\("Use clear\(\) instead\."\)\]\]\s*void flush\(\);', uh))
          and bool(re.search(r'\[\[deprecated\("Use clear\(\) instead\."\)\]\] void flush\(\)', stub_udp)), True)
    allit("NetworkUDP::parsePacket() nem blokkol (MSG_DONTWAIT)",
          bool(re.search(r'recvfrom\(udp_server, buf, 1460, MSG_DONTWAIT', uc)), True)
    allit("NetworkUDP::clear() eldobja a beolvasatlan maradekot",
          bool(re.search(r'void NetworkUDP::clear\(\) \{\s*if \(!rx_buffer\) \{\s*return;\s*\}\s*cbuf \*b = rx_buffer;\s*rx_buffer = 0;\s*delete b;', uc)), True)
    allit("NetworkUDP::parsePacket(): amig van beolvasatlan csomag, 0-t ad",
          bool(re.search(r'int NetworkUDP::parsePacket\(\) \{\s*if \(rx_buffer\) \{\s*return 0;', uc)), True)
    allit("stub: van WiFiUDP clear() es remoteIP()",
          bool(re.search(r'void clear\(\)', stub_udp) and re.search(r'IPAddress remoteIP\(\)', stub_udp)), True)
    allit("WiFiSTAClass::dnsIP(uint8_t dns_no = 0)",
          bool(re.search(r'IPAddress dnsIP\(uint8_t dns_no = 0\);', szoveg(keres(gyokerek, 'libraries/WiFi/src/WiFiSTA.h')))), True)
    allit("NetworkInterface::dnsIP(): netif nelkul 0.0.0.0",
          bool(re.search(r'IPAddress NetworkInterface::dnsIP\(uint8_t dns_no\) const \{\s*if \(_esp_netif == NULL', nic)), True)
    ia = szoveg(keres(gyokerek, 'cores/esp32/IPAddress.h'))
    allit("IPAddress::operator== es operator!= letezik",
          bool(re.search(r'bool operator==\(const IPAddress &addr\) const;', ia) and re.search(r'bool operator!=\(const IPAddress &addr\) const', ia)), True)

    aw = szoveg(keres(gyokerek, 'src/ESPAsyncWebServer.h'))
    allit("AsyncWebServerRequest::client() -> AsyncClient* (az allapotoldal cimszurese)",
          bool(re.search(r'AsyncClient \*client\(\) \{', aw)),
          bool(re.search(r'AsyncClient\* client\(\)', stub_aws)))
    at = szoveg(keres(gyokerek, 'src/AsyncTCP.h'))
    allit("AsyncClient::remoteIP() const -> IPAddress",
          bool(re.search(r'IPAddress remoteIP\(\) const;', at)),
          bool(re.search(r'IPAddress remoteIP\(\) const', stub_aws)))
    wap = szoveg(keres(gyokerek, 'libraries/WiFi/src/WiFiAP.h'))
    allit("WiFiAPClass::softAPdisconnect(bool wifioff = false)",
          bool(re.search(r'bool softAPdisconnect\(bool wifioff = false\);', wap)),
          bool(re.search(r'bool softAPdisconnect\(bool off = false\)', stub_wifi)))
    allit("WiFiAPClass::softAPSubnetMask() -> IPAddress",
          bool(re.search(r'IPAddress softAPSubnetMask\(\);', wap)),
          bool(re.search(r'IPAddress softAPSubnetMask\(\)', stub_wifi)))
    wtg = szoveg(keres(gyokerek, 'esp_wifi/include/esp_wifi_types_generic.h'))
    for nev, ertek in [('WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT', '15'), ('WIFI_REASON_NO_AP_FOUND', '201'),
                       ('WIFI_REASON_AUTH_FAIL', '202'), ('WIFI_REASON_HANDSHAKE_TIMEOUT', '204')]:
        allit(f"{nev} = {ertek} (a mentes utani rossz-jelszo felismeres)",
              egy(wtg, r'^\s*%s\s*=\s*(\d+)' % nev), egy(stub_wifi, r'^#define %s (\d+)' % nev))
    allit("wifi_event_sta_disconnected_t.reason: uint8_t",
          bool(re.search(r'uint8_t reason;\s*/\*\*< Disconnection reason \*/\s*int8_t\s+rssi;[^}]*\} wifi_event_sta_disconnected_t;', wtg)), True)
    ne = szoveg(keres(gyokerek, 'libraries/Network/src/NetworkEvents.h'))
    allit("arduino_event_info_t: wifi_sta_disconnected tag",
          bool(re.search(r'wifi_event_sta_disconnected_t wifi_sta_disconnected;', ne)), True)
    allit("WiFiGenericClass::onEvent(WiFiEventFuncCb, arduino_event_id_t)",
          bool(re.search(r'onEvent\(WiFiEventFuncCb cbEvent, arduino_event_id_t event = ARDUINO_EVENT_MAX\);',
                         szoveg(keres(gyokerek, 'libraries/WiFi/src/WiFiGeneric.h')))), True)
    allit("WIFI_AP_STA = WIFI_MODE_APSTA = 3 (a stub erteke)",
          bool(re.search(r'#define WIFI_AP_STA WIFI_MODE_APSTA', wt))
          and bool(re.search(r'WIFI_MODE_NULL = 0,[^\n]*\n\s*WIFI_MODE_STA,[^\n]*\n\s*WIFI_MODE_AP,[^\n]*\n\s*WIFI_MODE_APSTA,', wtg)),
          egy(stub_wifi, r'^#define WIFI_AP_STA (\d+)') == '3')
    allit("WiFiSTAClass::subnetMask() -> IPAddress",
          bool(re.search(r'IPAddress subnetMask\(\);', szoveg(keres(gyokerek, 'libraries/WiFi/src/WiFiSTA.h')))),
          bool(re.search(r'IPAddress subnetMask\(\)', stub_wifi)))

    if hibak:
        print("\nA STUB-IGAZOLAS MEGBUKOTT:")
        for h in hibak:
            print("  " + h)
        print("\nA stubok elteveredtek a valodi konyvtaraktol. Amig ez all, a")
        print("host-tesztek EREDMENYE NEM MEGBIZHATO: zold lehet ugy is, hogy az")
        print("eszkozon mas tortenik. Igazitsd a test/stubs/ alatti modellt.")
        return 1
    MIN_ALLITAS = 24
    if ellenorzesek < MIN_ALLITAS:
        print(f"\nA STUB-IGAZOLAS URESRE FUTOTT: csak {ellenorzesek} allitas "
              f"jott ossze (minimum {MIN_ALLITAS}).")
        print("Valoszinuleg a valodi headerek formaja valtozott, es a mintak")
        print("nem illeszkednek. Ez NEM sikeres ellenorzes.")
        return 1
    print(f"Stub-igazolas: rendben, {ellenorzesek} allitas egyezik a valodi "
          f"konyvtarakkal.")
    if nem_modellezett:
        print(f"  [info] {len(nem_modellezett)} szimbolumot szandekosan nem "
              f"modellezunk: {', '.join(nem_modellezett)}")
    return 0

if __name__ == '__main__':
    sys.exit(main())
