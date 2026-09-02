// Apro, tiszta szoveg-segedek.
//
// "Tiszta": nem olvasnak es nem irnak semmilyen globalis allapotot, csak a
// kapott puffert. Ezert barmelyik modul befoghatja anelkul, hogy a program
// tobbi reszehez kotne magat - es ezert ellenorizhetok onmagukban.
#pragma once

#include <stddef.h>

// A vezeto es a zaro whitespace levagasa HELYBEN.
//
// Miert kell tobb helyen? A masolas-beillesztes szinte mindig hoz egy zaro
// szokozt vagy sortorest: az urlapmezokbol (SSID, jelszo, IP, gateway) es a
// fajlbol beolvasott ertekekbol is. A vagas nelkul a "192.168.1.1 " ervenytelen
// cimnek latszana, a mentett SSID pedig nem egyezne a valodival.
void trimInPlace(char* s);
