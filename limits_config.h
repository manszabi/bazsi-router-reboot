// A konfiguracios mezok meretkorlatai.
//
// Sajat headerben, mert TOBB modul is fuggne toluk (a secret modul a jelszo
// hosszabol szamolja a kodolt alak maximumat), es egy meretkorlat olyan
// megallapodas, aminek egyetlen helyen kell allnia. Ez a header szandekosan
// SEMMI mast nem tartalmaz: nincs benne se allapot, se fuggveny, tehat barmely
// modul befoghatja anelkul, hogy fuggosegeket huzna magaval.
#pragma once

#include <stddef.h>

// A HTML urlaprol erkezo ertekek. Fix meretu bufferek: nincs heap-toredezettseg,
// es a szabvany szerinti maximumok egyben validaciot is jelentenek.
constexpr size_t SSID_MAX_LEN  = 32;  // IEEE 802.11 SSID
constexpr size_t PASS_MAX_LEN  = 63;  // WPA2-PSK passphrase
constexpr size_t IPSTR_MAX_LEN = 15;  // "255.255.255.255"
