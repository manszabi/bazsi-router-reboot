#!/usr/bin/env python3
"""A STUBOK IGAZOLASA A VALODI KONYVTARAK ELLEN.

MIERT KELL? Mert a teljes host-tesztkeszlet ervenyessege ezen all. Az 1761
ellenorzes mind a test/stubs/ alatti modelleken keresztul lat: ha egy stub
teved, a teszt ZOLD, az eszkoz meg hibazik - es semmi nem szolna. Ez a
tesztelesi piramis meg nem vizsgalt alapja.

Konkret pelda arra, mennyire eles ez: a rele a D10-en van. Ha a
XIAO_ESP32C3 variansaban a D10 szama valaha megvaltozik, a sketch tovabbra is
lefordul, MINDEN teszt zold marad (a teszt is a sajat konstansat hasznalja) -
es az eszkoz egy masik labat kapcsolgatna.

MIT ELLENORZUNK? Nem a stubok VISELKEDESET (azt nem lehet gepileg), hanem
azokat a SZAMOKAT, TIPUSOKAT es SZERKEZETEKET, amiket atvettunk: a labkiosztast,
a felsorolas-ertekeket, a struktura-mezoket, a fuggveny-szignaturakat. Plusz azt
a ket VISELKEDESI allitast, amire a forraskod kommentjei nev szerint
hivatkoznak (a WiFi first_connect logikaja es a loopTask per-iteracios etetese)
- ha azok elmozdulnak, a rajuk epulo indoklas is ervenyet veszti.

HASZNALAT:
    tools_stubcheck.py <gyoker> [<gyoker> ...]
A gyokerek alatt rekurzivan keressuk a szukseges fajlokat, tehat mindegy,
hogy arduino-cli telepitette oket vagy git klon.

TERVEZESI ELV, DRAGAN MEGTANULVA: ha egy keresett mintat NEM talalunk meg,
az HIBA, nem csendes atugras. Ebben a projektben mar ketszer fordult elo, hogy
egy ellenorzo azert volt "tiszta", mert nem vizsgalt semmit.
"""
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
    teszt       = szoveg(GYOKER / 'test/test_main.cpp')

    # --- 1. LABKIOSZTAS. A legelesebb: a rele lába. -------------------------
    pins = keres(gyokerek, 'variants/XIAO_ESP32C3/pins_arduino.h')
    ps = szoveg(pins)
    # A sketch ezeket a neveket hasznalja; a teszt a szamokat.
    LAB = {'D10': 'PIN_RELAY', 'D4': 'PIN_LED', 'D3': 'PIN_WIFILED',
           'D1': 'PIN_RESETBTN', 'D0': 'PIN_WIFIBTN'}
    for d, konst in LAB.items():
        valodi = egy(ps, r'^static const uint8_t %s\s*=\s*(\d+)\s*;' % d)
        mienk = egy(teszt, r'static const int %s\s*=\s*(\d+)\s*;' % konst)
        allit(f"labkiosztas {d} ({konst})", valodi, mienk)

    # --- 2. wl_status_t ertekek --------------------------------------------
    wt = szoveg(keres(gyokerek, 'libraries/WiFi/src/WiFiType.h'))
    # Amit NEM modellezunk, azt nem allitjuk - de a "nem modellezzuk" nem
    # tunhet el csendben: szamoljuk, es a vegen kimondjuk. Enelkul egy ures
    # stub is atmenne.
    for nev in ['WL_IDLE_STATUS', 'WL_NO_SSID_AVAIL', 'WL_CONNECTED',
                'WL_CONNECT_FAILED', 'WL_DISCONNECTED', 'WL_SCAN_COMPLETED',
                'WL_CONNECTION_LOST']:
        m = egy(stub_wifi, r'^#define %s (\d+)' % nev)
        if m is None:
            nem_modellezett.append(f"wl_status_t {nev}")
            continue
        allit(f"wl_status_t {nev}", egy(wt, r'^\s*%s\s*=\s*(\d+)' % nev), m)

    # --- 3. esp_reset_reason_t SORRENDJE ------------------------------------
    # Az ertekek implicitek, tehat a SORREND maga az ertek. Egy beszurt uj
    # elem minden utana allot elcsusztatna - es a watchdog-reset felismerese
    # eppen ezeken az ertekeken all.
    es = szoveg(keres(gyokerek, 'esp_system.h'))
    valodi_sorrend = re.findall(r'\bESP_RST_[A-Z_]+', es)
    stub_sorrend = re.findall(r'\bESP_RST_[A-Z_]+', stub_sys)
    n = min(len(valodi_sorrend), len(stub_sorrend))
    if n == 0:
        hibak.append("esp_reset_reason_t: egyik oldalrol sem sikerult kiolvasni")
    for i in range(n):
        allit(f"esp_reset_reason_t[{i}]", valodi_sorrend[i], stub_sorrend[i])

    # --- 4. esp_task_wdt_config_t mezoi (sorrendben, tipussal) -------------
    tw = szoveg(keres(gyokerek, 'esp_system/include/esp_task_wdt.h'))
    blokk = re.search(r'typedef struct \{(.*?)\} esp_task_wdt_config_t;', tw, re.S)
    sblokk = re.search(r'typedef struct \{(.*?)\} esp_task_wdt_config_t;', stub_wdt, re.S)
    if not blokk or not sblokk:
        hibak.append("esp_task_wdt_config_t: a struktura nem talalhato")
    else:
        vm = re.findall(r'\b(uint32_t|bool|int)\s+(\w+)\s*;', blokk.group(1))
        sm = re.findall(r'\b(uint32_t|bool|int)\s+(\w+)\s*;', sblokk.group(1))
        allit("esp_task_wdt_config_t mezoi", vm, sm)

    # --- 5. ESP_ERR kodok ---------------------------------------------------
    ee = szoveg(keres(gyokerek, 'esp_common/include/esp_err.h'))
    for nev in ['ESP_OK', 'ESP_FAIL', 'ESP_ERR_NO_MEM', 'ESP_ERR_INVALID_ARG',
                'ESP_ERR_INVALID_STATE', 'ESP_ERR_NOT_FOUND']:
        v = egy(ee, r'^#define %s\s+(-?\w+)' % nev)
        m = egy(stub_wdt, r'^#define %s (-?\w+)' % nev)
        if m is None:
            nem_modellezett.append(f"esp_err kod {nev}")
            continue
        allit(f"esp_err kod {nev}", v, m)

    # --- 6. HTTPClient idokorlat-szignaturak --------------------------------
    hc = szoveg(keres(gyokerek, 'libraries/HTTPClient/src/HTTPClient.h'))
    for fn in ['setConnectTimeout', 'setTimeout']:
        allit(f"HTTPClient::{fn} parametertipusa",
              egy(hc, r'void %s\((\w+)\s' % fn),
              egy(stub_http, r'void %s\((\w+)\)' % fn))

    # --- 7. Az ESPAsyncWebServer API, amin a POST-kezelo all ----------------
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

    # --- 8. A KET VISELKEDESI ALLITAS, amire a forraskod hivatkozik ---------
    # (a) A rossz jelszo EGYETLEN probalkozasbol lathatatlan, mert a core az
    #     elso disconnectet meg nem minositi hitelesitesi hibanak. A
    #     WifiSim::authFail modellje EZEN all.
    sta = szoveg(keres(gyokerek, 'libraries/WiFi/src/STA.cpp'))
    allit("STA.cpp: a 'first_connect' feltetel az AUTH_FAIL agon",
          bool(re.search(r'WIFI_REASON_AUTH_FAIL\)\s*&&\s*!first_connect', sta)),
          True)
    # (b) A loopTask MINDEN iteracio elott etet, ha fel van iratkozva. A
    #     harness coreLoopStep()-je ezt modellezi; ezen all a watchdog-res
    #     merese es a "visszater a loop()-ba" lint-kivetel is.
    mc = szoveg(keres(gyokerek, 'cores/esp32/main.cpp'))
    allit("main.cpp: a loopTask feltetelesen etet minden iteracioban",
          bool(re.search(r'if \(loopTaskWDTEnabled\)\s*\{\s*esp_task_wdt_reset\(\);', mc)),
          True)

    if hibak:
        print("\nA STUB-IGAZOLAS MEGBUKOTT:")
        for h in hibak:
            print("  " + h)
        print("\nA stubok elteveredtek a valodi konyvtaraktol. Amig ez all, a")
        print("host-tesztek EREDMENYE NEM MEGBIZHATO: zold lehet ugy is, hogy az")
        print("eszkozon mas tortenik. Igazitsd a test/stubs/ alatti modellt.")
        return 1
    # URESSEG-VEDELEM. Ha a keresett mintak barmelyik csaladja elnemul (mert a
    # valodi header formaja valtozott, vagy a stub kiurult), a szam leesik - es
    # ezt a kuszob megfogja. Ket ellenorzo mar volt ebben a projektben, ami
    # azert volt "tiszta", mert nem vizsgalt semmit; harmadik ne legyen.
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
