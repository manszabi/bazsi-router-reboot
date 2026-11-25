#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "limits_config.h"

constexpr uint32_t SECRET_SALT = 0x42415A53UL;
constexpr char SECRET_PREFIX[] = "v1:";
constexpr size_t SECRET_PREFIX_LEN = sizeof(SECRET_PREFIX) - 1;

constexpr size_t SECRET_ENC_MAX = SECRET_PREFIX_LEN + 2 * PASS_MAX_LEN;

bool encodeSecret(const char* plain, char* out, size_t outSize);

void decodeSecretInPlace(char* buf);
