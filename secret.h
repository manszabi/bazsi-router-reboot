// A mentett Wi-Fi jelszo osszekeverese a flashben.
//
// Ez a modul SZANDEKOSAN teljesen fuggetlen a program tobbi reszetol: nem
// olvas es nem ir egyetlen globalis allapotot sem, csak a kapott puffereken
// dolgozik (az egyetlen kulso bemenete a lapka eFuse MAC-je). Igy onmagaban
// is ertheto es ellenorizheto - es a fordito is kikenyszeriti, hogy az
// maradjon: amit ez a header nem hirdet meg, azt a modulon kivulrol nem lehet
// elerni.
#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "limits_config.h"

constexpr uint32_t SECRET_SALT = 0x42415A53UL;  // "BAZS"
constexpr char SECRET_PREFIX[] = "v1:";
constexpr size_t SECRET_PREFIX_LEN = sizeof(SECRET_PREFIX) - 1;
// "v1:" + 2 hexa jegy jelszo-bajtonkent
constexpr size_t SECRET_ENC_MAX = SECRET_PREFIX_LEN + 2 * PASS_MAX_LEN;

// Kodolas "v1:" + kisbetus hexa alakba. Hamis, ha a cel puffer keves.
bool encodeSecret(const char* plain, char* out, size_t outSize);

// Helyben dekodol. Ha a tartalom nem a mi formatumunk, VALTOZATLANUL hagyja.
void decodeSecretInPlace(char* buf);
