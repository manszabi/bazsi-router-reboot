// Amit a fomodul (a sketch) nyujt a tobbi modulnak.
//
// MIERT VAN ILYEN HEADER? A fuggosegek nem egyiranyuak: a halozati teszteknek
// szuksegunk van a watchdog etetesere es a gombok pollozasara, mert egy HTTP
// teszt masodpercekig futhat, es kozben sem a watchdog nem varhat, sem a
// felhasznalo gombnyomasa nem veszhet el. Ezt a nehany hivast a fomodul adja.
//
// A lista SZANDEKOSAN rovid, es szandekosan itt all kulon: ez a modulok
// "felfele" mutato fuggosege, es amint tobb lesz nala, az azt jelenti, hogy a
// hatarokat rosszul huztuk meg. Ma harom fuggveny.
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
