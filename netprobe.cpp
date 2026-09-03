#include "netprobe.h"

#include <HTTPClient.h>
#include <ESPping.h>
#include <string.h>

#include "app_hooks.h"
#include "strutil.h"

// A gomb-pollozas es a watchdog-etetes uteme a varakozo ciklusokban. Ugyanaz az
// ertek, mint a fomodul BUTTON_POLL_MS-e; itt sajat neven all, mert ez a modul
// csak ennyit hasznal belole, es igy nem kell a fomodul konstansaira huzodnia.
static constexpr uint32_t PROBE_POLL_MS = 10;
static_assert(PROBE_POLL_MS == 10, "a gomb-pollozas uteme a fomodullal egyezzen");

// Egy bájt, legfeljebb timeoutMs várakozással. -1: lejárt a határidő, vagy a
// szerver lezárta a kapcsolatot. Ez a kettő az egyetlen kilépési ok - a hívó
// mindkettőt "nincs több adat"-ként kezeli.
static int readByteBounded(WiFiClient& stream, uint32_t timeoutMs) {
  const uint32_t start = millis();
  while ((millis() - start) < timeoutMs) {
    // Csak akkor olvasunk, ha VAN mit: a read() a socket saját fogadási
    // timeoutját használja, ami nem a mienk. Ha vakon hívnánk, egyetlen
    // olvasás túlléphetné a timeoutMs határidőt - így viszont a határidő
    // valóban a miénk, és nem függ a core beállításaitól.
    if (stream.available() > 0) {
      return stream.read();  // <0 is lehet: available() ígért, de elszállt
    }
    // A szerver lezárta és nincs több adat: nincs értelme a timeoutot kivárni
    if (!stream.connected()) {
      return -1;
    }
    resetbutton();
    wifiresetbutton();
    feedWatchdog();
    // delay() és nem yield(): a yield() csak azonos prioritású taskok között
    // ad át vezérlést, tehát üresen pörgetné a CPU-t a válaszra várva.
    delay(PROBE_POLL_MS);
  }
  return -1;
}

// Korlátozott méretű, időzáras olvasás: nem allokál, és nem tud "elszállni"
// egy captive portal többszáz kilobájtos válaszán. A timeoutMs bájtok KÖZÖTTI
// határidő, tehát egy lassan csordogáló válasz is végigolvasható, egy néma
// kapcsolat viszont nem tart fel tovább egy timeoutnál.
static size_t readBounded(WiFiClient& stream, char* buf, size_t maxLen, uint32_t timeoutMs) {
  size_t n = 0;
  while (n < maxLen) {
    const int c = readByteBounded(stream, timeoutMs);
    if (c < 0) {
      break;
    }
    buf[n++] = (char)c;
  }
  return n;
}

// Egy hexa számjegy értéke, vagy -1. NAGYBETŰT IS elfogad, mert a chunked
// keretezés méret-sorai az RFC 9112 szerint bármelyik alakban jöhetnek.
//
// Ne keverd a hexVal()-lal: az a MI saját jelszó-kódolásunkat olvassa vissza,
// és szándékosan csak kisbetűset fogad el (azt írjuk ki). A két függvény
// neve korábban csak egy betűben tért el, a viselkedésük viszont nem -
// ezért kapott ez beszédesebb nevet.
static int hexDigitAnyCase(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Ugyanaz, mint a readBounded(), de lebontja a chunked keretezést.
//
// MIÉRT KELL: a HTTPClient a darabhatárokat CSAK a getString() /
// writeToStream() útján bontja le - azokat viszont nem használjuk, mert
// korlátlanul foglalnának. A nyers streamben tehát benne maradnak a
// keretbájtok ("7\r\nsuccess\r\n0\r\n\r\n"), és a strcmp() a tökéletesen
// működő végpontot is bukottnak látná. Content-Length-et küldő végpontnál ez
// sosem jön elő, de egy közbeiktatott proxy bármikor átkeretezheti a választ.
static size_t readChunked(WiFiClient& stream, char* buf, size_t maxLen, uint32_t timeoutMs) {
  size_t n = 0;
  for (uint16_t chunk = 0; chunk < HTTP_MAX_CHUNKS; chunk++) {
    // Méret sor: hexa szám, opcionális ";kiterjesztés", CRLF.
    uint32_t size = 0;
    bool sawDigit = false;
    bool inExt = false;
    for (;;) {
      const int c = readByteBounded(stream, timeoutMs);
      if (c < 0) return n;
      if (c == '\n') break;
      if (c == '\r' || inExt) continue;
      if (c == ';') { inExt = true; continue; }
      const int d = hexDigitAnyCase(c);
      // Nem hexa a méret helyén: ez nem chunked keret. Nem találgatunk,
      // a teszt elbukik - ez a biztonságos irány.
      if (d < 0) return n;
      if (size > (0xFFFFFFFFu - (uint32_t)d) / 16u) return n;  // túlcsordulás
      size = size * 16u + (uint32_t)d;
      sawDigit = true;
    }
    if (!sawDigit || size == 0) {
      return n;  // üres méret sor, vagy a lezáró 0-s darab
    }
    for (uint32_t i = 0; i < size; i++) {
      if (n >= maxLen) {
        return n;  // ekkora választ nem a várt végpont küld - nem olvassuk végig
      }
      const int c = readByteBounded(stream, timeoutMs);
      if (c < 0) return n;
      buf[n++] = (char)c;
    }
    // A darabot lezáró CRLF: elnyeljük, de nem kötjük meg magunkat a pontos
    // alakjában - a következő kör úgyis hexát vár.
    for (uint8_t i = 0; i < 2; i++) {
      const int c = readByteBounded(stream, timeoutMs);
      if (c < 0) return n;
      if (c == '\n') break;
    }
  }
  // SZERKEZETILEG ELERHETETLEN, ezert a lefedettsegben feher marad: minden nem
  // ures darab legalabb egy bajtot ad a pufferbe, tehat a fenti maxLen-korlat
  // (HTTP_MAX_PAYLOAD) mindig elobb ut, mint a HTTP_MAX_CHUNKS. A fordito is
  // ezt allitja - lasd a static_assertet a netprobe.h-ban. A sor megis kell:
  // a ciklus utan a fuggvenynek vissza kell adnia valamit.
  return n;
}

bool testInternetHTTP(const char* url, const char* expectedResponse) {
  WiFiClient client;
  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout((int32_t)HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout((uint16_t)HTTP_RESPONSE_TIMEOUT_MS);

  if (!http.begin(client, url)) {
    Serial.println("Error: HTTP begin failed");
    return false;
  }

  // A _transferEncoding privat, a nyers fejlec viszont igy elkerheto. Ez a
  // hivas a VALASZ fejleceire vonatkozik (a HTTPClient.h kommentje felrevezeto,
  // a handleHeaderResponse() tolti fel oket).
  static const char* kCollectHeaders[] = { "Transfer-Encoding" };
  http.collectHeaders(kCollectHeaders, 1);

  const int httpCode = http.GET();
  bool result = false;

  if (expectedResponse[0] == '\0') {
    // "generate_204" stilusu vegpont: nincs torzs, csak a statuszkod szamit.
    // Ez SZIGORUBB, mint a szoveg-egyeztetes: egy captive portal nem tud 204-et
    // adni, mert neki eppenseggel HTML-t vagy atiranyitast KELL kuldenie.
    // Ugyanezt a dontest hozza a NetworkManager is (nm-connectivity.c: 204 ->
    // "no content, as expected"; barmi mas -> portal).
    result = (httpCode == HTTP_CODE_NO_CONTENT);
    // Sikernel NEM irunk semmit: azt a hivo "Successful Test" sora mondja ki.
    // A soros portra csak az kerul ki, ami hibakeresesnel szamit.
    if (!result) {
      Serial.print("Error on HTTP request, code: ");
      Serial.println(httpCode);
    }
  } else if (httpCode == HTTP_CODE_OK) {
    // Chunked valasznal a getSize() -1, es a nyers streamben ott vannak a
    // keretbajtok is - azokat le kell bontani, kulonben a jo valasz is bukik.
    const bool chunked = http.header("Transfer-Encoding").equalsIgnoreCase("chunked");
    const int len = http.getSize();
    if (len > (int)HTTP_MAX_PAYLOAD) {
      // Ekkora választ nem a várt endpoint küld (pl. captive portal)
      Serial.print("Unexpected payload size: ");
      Serial.println(len);
    } else {
      // len == 0 (Content-Length: 0): nincs mit olvasni, várni sem kell rá.
      // len < 0: nincs Content-Length, a keretet a kapcsolat zárása adja.
      const size_t want = (len >= 0) ? (size_t)len : HTTP_MAX_PAYLOAD;
      char payload[HTTP_MAX_PAYLOAD + 1];
      // Ugyanaz a stream, amit a http.begin() kapott — nem függünk a
      // getStream() core-verziónként eltérő visszatérési típusától.
      const size_t n = chunked
                         ? readChunked(client, payload, HTTP_MAX_PAYLOAD, HTTP_READ_TIMEOUT_MS)
                         : readBounded(client, payload, want, HTTP_READ_TIMEOUT_MS);
      payload[n] = '\0';
      trimInPlace(payload);  // a záró CR/LF ne buktassa el az egyezést
      result = (strcmp(payload, expectedResponse) == 0);
      // Csak eltéréskor beszélünk. Ilyenkor viszont a KAPOTT törzs a
      // legfontosabb információ: abból derül ki, hogy captive portál ült-e
      // a kérésre, vagy az üzemeltető változtatta meg a választ.
      if (!result) {
        Serial.println(payload);
        Serial.println("Hamis érték!");
      }
    }
  } else {
    Serial.print("Error on HTTP request, code: ");
    Serial.println(httpCode);
  }

  http.end();
  return result;
}

bool testInternetPing(const IPAddress& target, const char* targetName) {
  Serial.print("Ping teszt futtatása (");
  Serial.print(targetName);
  Serial.print(" - ");
  Serial.print(target);
  Serial.println(")...");
  uint8_t successCount = 0;

  for (uint8_t j = 0; j < PING_ATTEMPTS; j++) {
    // A Ping.ping() érték szerint veszi a címet (ESPping: bool ping(IPAddress,
    // int16_t)), tehát a másolatot ő maga készíti - nem kell külön helyi példány.
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

  // Ide nem lehet eljutni: 4 probaval es 2-es kuszobbel a ciklus mindig a ket
  // korai return valamelyiken lep ki (a j=2 koron a 0 sikeres mar elbukott, a
  // j=3-on a 2. siker mar visszateres). A fordito viszont megkoveteli, ezert
  // ez a sor a lefedettsegben mindig fehér marad.
  return successCount >= PING_MIN_SUCCESS;
}
