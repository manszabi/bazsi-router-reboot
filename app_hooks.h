// Amit a fomodul (a sketch) nyujt a tobbi modulnak.
//
// MIERT VAN ILYEN HEADER? A fuggosegek nem egyiranyuak: a halozati teszteknek
// szuksegunk van a watchdog etetesere es a gombok pollozasara, mert egy HTTP
// teszt masodpercekig futhat, es kozben sem a watchdog nem varhat, sem a
// felhasznalo gombnyomasa nem veszhet el. Ezt a nehany hivast a fomodul adja.
//
// A lista SZANDEKOSAN rovid, es szandekosan itt all kulon: ez a modulok
// "felfele" mutato fuggosege, es amint tobb lesz nala, az azt jelenti, hogy a
// hatarokat rosszul huztuk meg. Ma negy fuggveny es egy lekerdezes.
// (Volt kozottuk egy filesystemReady() is - az azota a configstore modulba
// kerult, oda, ahova valo. Pontosan ezert hasznos ez a header.)
#pragma once

#include <stdint.h>

// A task watchdog etetese. Csak akkor tesz barmit, ha a loop task mar fel van
// iratkoztatva - a modulok tehat feltetel nelkul hivhatjak.
void feedWatchdog();

// A ket gomb pollozott ellenorzese. Egy hosszan blokkolo szakasz alatt is
// eljon a sor: ezert kell oket a varakozo ciklusokbol hivni.
void resetbutton();
void wifiresetbutton();

// Varakozas ugy, hogy kozben a gombok es a watchdog is elnek. Egy halozati
// teszt szuneteire kell: delay()-jel a gombnyomas elveszne, busy-loop-pal
// pedig a CPU porogne.
void waitWithButtons(uint32_t duration);

// Az eltelt uzemido kiirasa a soros portra. A modulok uzenetei igy ugyanabban
// az alakban jelennek meg, mint a fomodulei - egy soros naplot csak igy lehet
// utolag ertelmezni.
void printUptime();

// A fomodul allapotabol az, amit a diagnosztikai lap MUTAT.
//
// Miert EGY lekerdezes, nem negy kulon getter? Mert ez egy fogalom: "mit lat
// az, aki most nyitja meg a /log oldalat". Negy kulon fuggveny negy uj
// fuggoseget jelentene ugyanarra, es a portal negy kulon idopillanatban
// olvasna ki oket. Igy egyszerre, egy egeszkent kapja meg.
//
// A szamlalok maguk a fomodulban maradnak: a watchdog-politika es az
// ujraprobalkozasi ablak az o dontesei, a portal csak megjeleniti oket.
struct DiagCounters {
  uint32_t wdtResets;    // hany rendellenes ujraindulas volt sorozatban
  uint32_t wdtLimit;     // ...es hanynal megyunk vegzetes hibaba
  uint32_t retryRounds;  // hany ujraprobalkozasi kor telt el
  uint32_t retryLimit;   // ...es hanynal megyunk AP modba
};
DiagCounters diagCounters();
