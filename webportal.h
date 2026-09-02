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

// A tetlensegi visszaszamlalas ujrainditasa. Minden HTTP keresnel megtortenik;
// kivulrol azert latszik, mert a fomodul is kitolja a hataridot, amikor AP
// modba lep.
void touchApDeadline();
