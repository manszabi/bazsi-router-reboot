#include "sync.h"

#include "app_hooks.h"

// A kozos spinlock. Miert EGY zar mindenre? Mert ket kulon zarnal a
// megszerzesi sorrend kerdesse valna, es a rossz sorrend holtpont. Egy zarral
// ez a hibaosztaly nem letezik. Az ar elhanyagolhato: minden szakasz nehany
// utasitas, es a mux csak a ket task kozott versenyzik.
portMUX_TYPE evLogMux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------------
// HAROM ESETET KELL MEGKULONBOZTETNI, es NEM ugyanaz a szabaly vonatkozik rajuk:
//
// 1. EGYETLEN SZO, ONALLO JELENTESSEL - apDeadline.
//    Egy igazitott 32 bites olvasas/iras oszthatatlan, es ennek a valtozonak
//    nincs masik valtozoval kozos invariansa: barmelyik erteket latja is az
//    olvaso (a regit vagy az ujat), az onmagaban ervenyes hatarido. Itt a
//    volatile eleg - az a dolga, hogy a fordito tenylegesen olvassa ki, ne
//    tartsa regiszterben. Nem kell zar, ezert ez az egyetlen valtozo, ami
//    kozvetlenul is elerheto marad (extern a headerben).
//
// 2. EGY JELZO, AMIN ZAR MULIK - savingConfig.
//    Itt nem az olvasas oszthatatlansaga a kerdes, hanem hogy a "megnezem,
//    aztan beallitom" ket lepese kozott a masik task ne ferhessen be. Ezt
//    teszi atomikussa a beginConfigWrite() / endConfigWrite() par.
//
// 3. KET VALTOZO, KOZOS INVARIANSSAL - restartAt + restartPending.
//    EZ AZ EGYETLEN VALODI VESZELY a programban, es ez a modul miatta all itt.
//
//    Az async_tcp task ket KULON irast vegezne:
//        restartAt = millis() + RESTART_GRACE_MS;
//        restartPending = true;
//    a loop task pedig a kettot EGYUTT olvasna ki es egyben dontene beloluk:
//        if (restartPending && (int32_t)(now - restartAt) >= 0) ...
//
//    Ha az olvaso a jelzot mar igaznak latja, de a hataridot meg a REGI
//    ertekevel, akkor ervenytelen paron dont. A restartAt kezdoerteke 0, tehat
//    a "now - 0 >= 0" gyakorlatilag mindig igaz: az eszkoz AZONNAL ujraindulna,
//    meg mielott a 200-as valasz kiment volna a bongeszonek. A felhasznalo azt
//    latna, hogy a mentes "nem valaszolt" - kozben tokeletesen elmentette.
//
//    OSZINTEN A SULYOSSAGROL: ESP32-C3-on ez ma NEM fordul elo. A chip
//    EGYMAGOS, tehat a ket task ugyanazon a magon, egymast valtva fut (nincs
//    egyideju iras es olvasas), a volatile-volatile irasokat pedig a fordito
//    sem rendezheti at egymashoz kepest. A helyesseg tehat ADOTT volt - csak
//    nem a kodbol kovetkezett, hanem a hardver egy tulajdonsagabol, amit a
//    nyelv nem garantal es amit a program sehol nem mondott ki. Egy ketmagos
//    ESP32-re (S3) portolva a vedelem SZO NELKUL eltunt volna, es a hiba ritka,
//    nem reprodukalhato ujraindulaskent jelentkezne - a legrosszabb fajta.
//
//    Ezert a part ugyanaz a spinlock vedi: a ket iras es a ket olvasas egy-egy
//    oszthatatlan szakaszba kerul. Merve: SYNC1-SYNC3 (mutaciosan igazolva).
// ---------------------------------------------------------------------------

// A ket task kozott osztott allapot. STATIC: a modulon kivulrol nem elerheto,
// csak az alabbi fuggvenyeken at - ezt mar a linker kenyszeriti ki.
static volatile bool     savingConfig   = false;
static volatile bool     restartPending = false;
static volatile uint32_t restartAt      = 0;

// AP beallito mod: mikor aludjon el, ha nem erkezik mentes. Minden HTTP keres
// kitolja. Lasd fent az 1. esetet: ez marad kozvetlenul elerheto.
volatile uint32_t apDeadline = 0;

// A konfigfájloknak egyszerre csak EGY írója lehet: vagy a webes mentés
// (async_tcp task), vagy a wifireset gomb törlése (loop task). A zár maga a
// savingConfig jelző - a megszerzését viszont atomikussá kell tenni, mert a
// puszta "if (savingConfig)" ellenőrzés és az írás megkezdése között a másik
// task közbeléphetne, és a két író ugyanazokat a fájlokat írná egyszerre.
// A rövid kritikus szakaszhoz ugyanazt a spinlockot használjuk, mint a napló.
bool beginConfigWrite() {
  bool acquired = false;
  portENTER_CRITICAL(&evLogMux);
  if (!savingConfig) {
    savingConfig = true;
    acquired = true;
  }
  portEXIT_CRITICAL(&evLogMux);
  return acquired;
}

// A zar feloldasa. Parja a beginConfigWrite()-nak, es SZANDEKOSAN ugyanazt a
// spinlockot hasznalja, pedig egyetlen bool irasa onmagaban is oszthatatlan.
// Ket okbol:
//
//  - FELSZABADITASI SORREND. A feloldas azt jelenti ki, hogy a fajlirasok
//    befejezodtek. Ha a masik task a jelzot mar hamisnak latja, latnia kell
//    mindent, ami a zar alatt tortent. A kritikus szakasz ezt a sorrendet
//    kikenyszeriti; a puszta volatile iras nem.
//  - SZIMMETRIA. Zarat szerezni fuggvennyel, elengedni viszont egy szabadon
//    allo "savingConfig = false;" sorral harom kulonbozo helyen: pontosan igy
//    marad el valahol a feloldas egy kesobbi modositasnal.
void endConfigWrite() {
  portENTER_CRITICAL(&evLogMux);
  savingConfig = false;
  portEXIT_CRITICAL(&evLogMux);
}

// ---------------------------------------------------------------------------
// A KET TASK KOZOTTI OSZTOTT ALLAPOT - es miert epp ez a harom fuggveny van.
//
// A programban ket FreeRTOS task fut: a loop task, es az AsyncTCP sajat
// "async_tcp" taskja, ami a webszervert szolgalja ki. Harom fele osztott
// valtozo van, es NEM ugyanaz a szabaly vonatkozik rajuk:
//
// 1. EGYETLEN SZO, ONALLO JELENTESSEL - apDeadline.
//    Egy igazitott 32 bites olvasas/iras oszthatatlan, es ennek a valtozonak
//    nincs masik valtozoval kozos invariansa: barmelyik erteket latja is az
//    olvaso (a regit vagy az ujat), az onmagaban ervenyes hatarido. Itt a
//    volatile eleg - az a dolga, hogy a fordito tenylegesen olvassa ki, ne
//    tartsa regiszterben. Nem kell zar.
//
// 2. EGY JELZO, AMIN ZAR MULIK - savingConfig.
//    Itt nem az olvasas oszthatatlansaga a kerdes, hanem hogy a "megnezem,
//    aztan beallitom" ket lepese kozott a masik task ne ferhessen be. Ezt
//    teszi atomikussa a beginConfigWrite() / endConfigWrite() par.
//
// 3. KET VALTOZO, KOZOS INVARIANSSAL - restartAt + restartPending.
//    EZ AZ EGYETLEN VALODI VESZELY a programban, es ez a harom fuggveny
//    miatta all itt.
//
//    Az async_tcp task ket KULON irast vegez:
//        restartAt = millis() + RESTART_GRACE_MS;
//        restartPending = true;
//    a loop task pedig a kettot EGYUTT olvassa ki es egyben dont beloluk:
//        if (restartPending && (int32_t)(now - restartAt) >= 0) ...
//
//    Ha az olvaso a jelzot mar igaznak latja, de a hataridot meg a REGI
//    ertekevel, akkor ervenytelen paron dont. A restartAt kezdoerteke 0, tehat
//    a "now - 0 >= 0" gyakorlatilag mindig igaz: az eszkoz AZONNAL ujraindulna,
//    meg mielott a 200-as valasz kiment volna a bongeszonek. A felhasznalo azt
//    latna, hogy a mentes "nem valaszolt" - kozben tokeletesen elmentette.
//
//    ESP32-C3-on ez ma NEM fordul elo, es ezt fontos oszinten kimondani: a
//    chip EGYMAGOS, tehat a ket task ugyanazon a magon, egymast valtva fut
//    (nincs egyideju iras es olvasas), a volatile-volatile irasokat pedig a
//    fordito sem rendezheti at egymashoz kepest. A helyesseg tehat ADOTT -
//    csak nem a kodbol kovetkezik, hanem a hardver egy tulajdonsagabol,
//    amit a nyelv nem garantal, es amit a program sehol nem mondott ki.
//
//    Ez ket dolgot jelent. Eloszor: egy ketmagos ESP32-re (S3, vagy a
//    klasszikus ESP32) portolva a vedelem SZO NELKUL eltunne, es a hiba ritka,
//    nem reprodukalhato ujraindulaskent jelentkezne - a legrosszabb fajta.
//    Masodszor: egy jovobeli olvasonak semmi nem arulna el, hogy itt egyaltalan
//    kerdes volt. Ezert a part ugyanaz a spinlock vedi, ami a naplot: a ket
//    iras es a ket olvasas egy-egy oszthatatlan szakaszba kerul. Az ar nehany
//    utasitas egy 10 ms-onkent futo cikluson; a nyereseg az, hogy a helyesseg
//    innentol a kodbol kovetkezik.
//
// A megszakitas-kezelokkel osztott jelzok (btnResetLatched, btnWifiResetLatched)
// SZANDEKOSAN maradnak sima volatile bool-ok: ez az 1. eset. A hozzajuk tartozo
// idobelyegeket (btnResetDownAt, btnWifiResetDownAt) rajtuk kivul SENKI nem
// olvassa - kizarolag az ISR irja es olvassa -, tehat task oldalon nincs par,
// amit egyutt kellene latni.
// ---------------------------------------------------------------------------

// Halasztott ujraindulas kerese az async_tcp taskbol.
// A millis() SZANDEKOSAN a kritikus szakaszon KIVUL fut: a szakaszban csak a
// ket iras all, semmi tobb.
void requestRestart(uint32_t delayMs) {
  const uint32_t at = millis() + delayMs;
  portENTER_CRITICAL(&evLogMux);
  restartAt = at;
  restartPending = true;
  portEXIT_CRITICAL(&evLogMux);
}

// Van-e ervenyes ujraindulasi keres, es letelt-e a turelmi ido? A jelzot es a
// hataridot EGYUTT olvassuk - ez a fuggveny letezesenek egyetlen oka.
bool restartRequestDue(uint32_t now) {
  portENTER_CRITICAL(&evLogMux);
  const bool due = restartPending && (int32_t)(now - restartAt) >= 0;
  portEXIT_CRITICAL(&evLogMux);
  return due;
}

// Van-e egyaltalan folyamatban levo ujraindulasi keres? (Az AP mod tetlensegi
// elalvasa nezi: ujraindulas elott nem alszunk el.) Itt nincs par - egyetlen
// jelzo -, de ugyanazon az uton kerdezzuk, mint a tobbit.
bool restartRequested() {
  portENTER_CRITICAL(&evLogMux);
  const bool p = restartPending;
  portEXIT_CRITICAL(&evLogMux);
  return p;
}

// A keres torlese. Csak az ujraindulas elott fut, tehat a hatasa maganak az
// ujraindulasnak amugy is elveszne - de a jelzot nem hagyjuk igazan allva egy
// olyan uton sem, ahol az ESP.restart() barmiert visszaterne.
void clearRestartRequest() {
  portENTER_CRITICAL(&evLogMux);
  restartPending = false;
  portEXIT_CRITICAL(&evLogMux);
}

// Folyamatban levo fajliras? (Ugyanaz a megfontolas, mint fent.)
bool configWriteInProgress() {
  portENTER_CRITICAL(&evLogMux);
  const bool s = savingConfig;
  portEXIT_CRITICAL(&evLogMux);
  return s;
}
