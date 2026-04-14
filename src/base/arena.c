#include "arena.h"

size_t align_forward(size_t x, size_t align) {
  size_t mask = align - 1;
  return (x + mask) & ~mask;
}

void *arena_alloc(Arena *a, size_t size) {
  size_t start = align_forward(a->len, ARENA_DEFAULT_ALIGN);
  size_t end = start + size;

  if (end > a->cap) {
    __builtin_trap(); // out of memory, should probably double arena size
  }

  void *ptr = a->base + start;
  a->len = end;
  return ptr;
}

void arena_reset(Arena *a) { a->len = 0; }
