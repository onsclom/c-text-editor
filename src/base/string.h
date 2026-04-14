#pragma once

#include "core.h"

typedef struct {
  u8 *data;
  size_t length;
} s8;

#define s8_lit(str) (s8){.data = (u8 *)(str), .length = sizeof(str) - 1}

bool s8_eq(s8 a, s8 b);

bool is_alpha(u8 c);
bool is_digit(u8 c);
bool is_alnum(u8 c);
