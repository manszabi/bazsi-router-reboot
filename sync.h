// A KET TASK KOZOTTI OSZTOTT ALLAPOT.
//
// A programban ket FreeRTOS task fut: a loop task, es az AsyncTCP sajat
// "async_tcp" taskja, ami a webszervert szolgalja ki. Ez a modul birtokolja azt
// a nehany valtozot, amit MINDKETTO lat, es rajta kivul senki nem nyulhat
// hozzajuk kozvetlenul - a fordito mar ki is kenyszeriti: a valtozok a
// sync.cpp-ben statikusak, csak az itt meghirdetett fuggvenyeken at erhetok el.
//
// (Az egyetlen kivetel az apDeadline es az evLogMux, lasd lent.)
#pragma once

#include <Arduino.h>
#include <stdint.h>

// A kozos spinlock. A naplo (eventlog) is ezt hasznalja: a ket task kozotti
// MINDEN rovid kritikus szakasz ugyanaz a zar, igy nem alakulhat ki
// zar-sorrendbol fakado holtpont - egyszeruen nincs masodik zar.
extern portMUX_TYPE evLogMux;

// --- Konfigzar -------------------------------------------------------------
// A konfigfajloknak egyszerre csak EGY iroja lehet: vagy a webes mentes
// (async_tcp task), vagy a wifireset gomb torlese (loop task).

// A zar ATOMIKUS megszerzese. Igaz, ha a mienk lett.
bool beginConfigWrite();
// A zar feloldasa. Mindig a beginConfigWrite() parjakent.
void endConfigWrite();
// Folyamatban van-e epp fajliras?
bool configWriteInProgress();

// --- Halasztott ujraindulas ------------------------------------------------
// Az async_tcp callbackbol nem szabad blokkolni vagy ujraindulni, ezert csak
// jelzunk; az ujraindulast a loop task vegzi el, miutan a valasz kiment.

// Ujraindulas kerese delayMs turelmi idovel.
void requestRestart(uint32_t delayMs);
// Van-e ervenyes keres, ES letelt-e a turelmi ido? A jelzot es a hataridot
// EGYUTT olvassa - ez a fuggveny letezesenek egyetlen oka.
bool restartRequestDue(uint32_t now);
// Van-e egyaltalan folyamatban levo keres?
bool restartRequested();
// A keres torlese.
void clearRestartRequest();

// --- AP tetlensegi hatarido ------------------------------------------------
// SZANDEKOSAN sima volatile valtozo, nem fuggvenyek mogott: onallo jelentesu
// egyetlen szo, aminek nincs masik valtozoval kozos invariansa. Barmelyik
// erteket latja is az olvaso, az onmagaban ervenyes hatarido. A reszletes
// indoklas a sync.cpp-ben.
extern volatile uint32_t apDeadline;
