#pragma once
#include <stddef.h>

static inline size_t next_pow2(size_t n) {
  if (n <= 1) {
    return 1;
  }

  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;
  return n + 1;
}
