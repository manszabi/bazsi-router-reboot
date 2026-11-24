// AZ AP BEALLITO WEBPORTAL: a HTTP felulet.
//
// Ez a program biztonsagkritikus felulete - ezen megy be a Wi-Fi jelszo -,
// ezert kulon all es szoros a felulete: kivulrol OSSZESEN harom fuggveny
// latszik belole. A LittleFS-rol SZANDEKOSAN nem szolgalunk ki semmit (egy
// serveStatic("/") a /pass.txt-t is kiadta volna), a beallito urlap a
// programba van forditva.
//
// A modul a HTTP feluletet birtokolja, NEM az uzemmodot: hogy mikor kell AP
// modba menni, azt a fomodul donti el (startConfigPortal), es az allitja a
// LED-eket is. Ez a modul csak azt tudja, hogyan nez ki es hogyan viselkedik
// a portal.
#pragma once

#include <ESPAsyncWebServer.h>
#include <stdint.h>

// Az AP mod tetlensegi ideje. Minden HTTP keres (a 404 is) kitolja.
constexpr uint32_t AP_TIMEOUT_MS = 5 * 60 * 1000;

// A hozzaferesi pont elinditasa es a HTTP vegpontok bejegyzese.
// A hivo felelossege, hogy az uzemmodot es a jelzeseket mar beallitotta.
void startWebPortal();

// A webszerver leallitasa (alvas es ujraindulas elott).
void stopWebPortal();

// AZ ESZKOZ SAJAT AP JELSZAVA.
//
// Korabban minden eszkoz ugyanazt, a forraskodban olvashato "12345678"-at
// hasznalta. Ez ma kifejezetten tilos: az ETSI EN 303 645 (5.1-1), az EU
// radioberendezes-iranyelvenek 2025. augusztus 1. ota kotelezo
// kiberbiztonsagi kovetelmenyei (EN 18031) es a brit PSTI torveny is tiltja az
// "univerzalis alapertelmezett jelszot". Egy szomszed a portalon at at tudta
// volna irni a Wi-Fi beallitast - es ez a portal az, amin a Wi-Fi jelszo
// bemegy.
//
// Most minden eszkoz ELSO portal-inditasakor egy 12 karakteres, valodi
// veletlen jelszot general (esp_random(): bekapcsolt Wi-Fi mellett hardveres
// veletlen, esp_random.h), es kodolva menti (/appass.txt, ugyanaz a kodolas,
// mint a Wi-Fi jelszonal). Utana mindig ezt hasznalja: a wifireset gomb sem
// torli, tehat egy felcimkezett jelszo ervenyes marad.
//
// A soros portra MINDEN indulaskor kiirjuk (ha mar van), es generalaskor is -
// a soros port fizikai hozzaferest igenyel, ez a tervezett ut a megismeresere.
void announceApPassword();

// A tetlensegi visszaszamlalas ujrainditasa. Minden HTTP keresnel megtortenik;
// kivulrol azert latszik, mert a fomodul is kitolja a hataridot, amikor AP
// modba lep.
void touchApDeadline();
