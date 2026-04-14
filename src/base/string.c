#include "string.h"

bool s8_eq(s8 a, s8 b) {
  if (a.length != b.length)
    return false;
  return base_memcmp(a.data, b.data, a.length) == 0;
}

bool is_alpha(u8 c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_digit(u8 c) { return c >= '0' && c <= '9'; }

bool is_alnum(u8 c) { return is_alpha(c) || is_digit(c) || c == '_'; }
