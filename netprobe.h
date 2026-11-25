// Internetkapcsolat merese: HTTP vegpont-tesztek es ICMP ping.
//
// A modul csak MER - nem dont, nem naploz, nem nyul a relehez es nem valtoztat
// allapotot. A dontes (mikor kell router reset, mikor AP mod) a fomodul
// allapotgepe. Ez a szetvalasztas teszi lehetove, hogy a tesztek a merest
// onmagaban ellenorizzek.
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <stddef.h>
#include <stdint.h>

// --- Ping parameterek ---
// Tobb probat teszunk es tobbsegi dontest hozunk: egyetlen elveszett ICMP
// csomag (ami vezetek nelkul teljesen normalis) ne latszodjon kiesesnek.
constexpr uint8_t  PING_ATTEMPTS    = 4;
constexpr uint8_t  PING_MIN_SUCCESS = 2;
constexpr uint32_t PING_GAP_MS      = 1000;

// --- HTTP parameterek ---
constexpr size_t   HTTP_MAX_PAYLOAD         = 96;  // a vart valaszok < 32 bajt
constexpr uint32_t HTTP_CONNECT_TIMEOUT_MS  = 5000;
constexpr uint32_t HTTP_RESPONSE_TIMEOUT_MS = 10000;
constexpr uint32_t HTTP_READ_TIMEOUT_MS     = 1500;
// Chunked valasznal hany darabot vagyunk hajlandok vegigolvasni. Minden nem
// lezaro darab legalabb 1 bajtot ad, es a puffer hataran ugyis megallunk, tehat
// ennyi kort elmeletileg sem lehet tullepni - ez csak egy vegso kapaszkodo,
// nehogy egy szabalytalan keretezes vegtelen ciklusba vigyen.
constexpr uint16_t HTTP_MAX_CHUNKS = HTTP_MAX_PAYLOAD + 2;
// A "+2" NEM veletlen, es nem is kozomobos. Mivel minden NEM URES darab
// legalabb egy bajtot ad a pufferbe, a hasznos-teher korlat (HTTP_MAX_PAYLOAD)
// MINDIG elobb ut, mint a darabszam-korlat. Ennek ket kovetkezmenye van:
//  - egy vegtelen darab-folyam nem tud a memoriaba irni, csak korozni, es ezt
//    a darabszam-korlat vagja el;
//  - a readChunked() ciklus utani "return n;" ezert SZERKEZETILEG
//    ELERHETETLEN, es a lefedettsegben szandekosan feher marad.
// A masodik allitas csak addig igaz, amig ez a ket konstans igy all egymashoz -
// eddig ezt semmi nem tartotta be, mostantol a fordito.
static_assert(HTTP_MAX_CHUNKS > HTTP_MAX_PAYLOAD,
              "a darabszam-korlat legyen tagabb a hasznos-teher korlatnal, "
              "kulonben a readChunked() zaro return-je elerhetove valik");

// Egy vegpont tesztje. Ures expectedResponse = 204-es (No Content) ellenorzes.
// Igaz, ha a valasz a vartnak megfelel.
bool testInternetHTTP(const char* url, const char* expectedResponse);

// AZ UTOLSO HTTP TESZT KODJA - a diagnozis legfontosabb adata.
//
// MIERT KELL? Korabban a hibakod CSAK a soros portra ment ki. Egy terepen
// dolgozo eszkoznel soros kabel nincs, a naplo pedig csak annyit orzott meg,
// hogy "a 3. teszt bukott" - azt nem, hogy MIERT. Pedig a kod epp ezt mondja
// meg: -1 = nem sikerult kapcsolodni (ide tartozik a bukott nevfeloldas is,
// mert a core ilyenkor 0.0.0.0-ra probal kapcsolodni), -11 = a szerver nem
// valaszolt idoben, 200 = jott valasz, de nem a vart szoveg (captive portal,
// szuro), 403/5xx = a szerver vagy egy kozbeiktatott szuro utasitotta el.
//
// Ertekek: a HTTPClient kodja (negativ = kliens oldali hiba), vagy
// HTTP_CODE_BEGIN_FAILED, ha a kerest el sem lehetett inditani.
constexpr int HTTP_CODE_BEGIN_FAILED = -100;
int lastHttpCode();

// A kod NAPLOZHATO alakja (az esemenynaplo parametere uint16_t). A nemnegativ
// kod valtozatlan (200, 204, 403...), a negativ kod 1000 + |kod| (-1 -> 1001,
// -11 -> 1011, a begin hiba -> 1100). Igy a /log lapon ember is olvashatja.
uint16_t httpCodeLogParam(int code);

// Nevfeloldas-proba: igaz, ha a host IPv4 cimre feloldhato.
//
// FIGYELEM, a core csapdaja: a hostByName() hibanal NEM 0-t ad, hanem a lwIP
// POZITIV hibakodjat (NetworkManager.cpp: "return err;"), tehat a puszta
// igazsagertek siker volna. Ezert a visszateresi ERTEK (1) es a cim is szamit.
bool testDnsResolve(const char* host);

// ICMP ping tobbsegi dontessel (PING_ATTEMPTS probabol PING_MIN_SUCCESS siker).
bool testInternetPing(const IPAddress& target, const char* targetName);
