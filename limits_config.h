// A konfigurációs mezők méretkorlátai (IEEE 802.11 SSID, WPA2 jelszó, IPv4 szöveg).
#pragma once

#include <stddef.h>

constexpr size_t SSID_MAX_LEN  = 32;
constexpr size_t PASS_MAX_LEN  = 63;
constexpr size_t IPSTR_MAX_LEN = 15;
