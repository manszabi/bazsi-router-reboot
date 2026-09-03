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

// ICMP ping tobbsegi dontessel (PING_ATTEMPTS probabol PING_MIN_SUCCESS siker).
bool testInternetPing(const IPAddress& target, const char* targetName);
