#pragma once

#include "core.h"

typedef struct {
  u8 *base;
  size_t cap;
  size_t len;
} Arena;

#define ARENA_DEFAULT_ALIGN 8

size_t align_forward(size_t x, size_t align);
void *arena_alloc(Arena *a, size_t size);
void arena_reset(Arena *a);
