#pragma once
#include "vector.h"
#include <stddef.h>
#include <string.h>

typedef struct {
  const char *data;
  size_t length;
} StringView;

static inline StringView sv_build(const char *s) {
  return (StringView){.data = s, .length = strlen(s)};
}

static inline StringView sv_from_parts(const char *data, size_t length) {
  return (StringView){.data = data, .length = length};
}

static inline bool sv_eq(StringView a, StringView b) {
  if (a.length != b.length) {
    return false;
  }
  return memcmp(a.data, b.data, a.length) == 0;
}

static inline bool sv_starts_with(StringView s, StringView prefix) {
  if (s.length < prefix.length) {
    return false;
  }
  return memcmp(s.data, prefix.data, prefix.length) == 0;
}

static inline size_t sv_hash(StringView s) {
  size_t hash = 5381;
  for (size_t i = 0; i < s.length; i++) {
    hash = ((hash << 5) + hash) + (unsigned char)s.data[i];
  }
  return hash;
}

DEFINE_VECTOR(StringView, StrViewVector)
DEFINE_VECTOR(char, CharVector)
