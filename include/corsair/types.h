#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
  uint8_t bytes[20];
} Sha1Hash;

typedef struct {
  uint8_t bytes[20];
} PeerId;

typedef struct {
  const char *data;
  size_t length;
} StringView;

constexpr StringView SV_NULL = (StringView){.data = nullptr, .length = 0};

static inline StringView sv_from_cstr(const char *s) {
  return (StringView){.data = s, .length = strlen(s)};
}

static inline StringView sv_build(const char *data, size_t length) {
  return (StringView){.data = data, .length = length};
}

static inline bool sv_eq(StringView a, StringView b) {
  return a.length == b.length && memcmp(a.data, b.data, a.length) == 0;
}

static inline bool sv_eq_cstr(StringView a, const char *s) {
  size_t len = strlen(s);
  return a.length == len && memcmp(a.data, s, len) == 0;
}

static inline bool sv_starts_with(StringView s, StringView prefix) {
  return s.length >= prefix.length &&
         memcmp(s.data, prefix.data, prefix.length) == 0;
}

static inline size_t sv_hash(StringView s) {
  size_t hash = 5381;
  for (size_t i = 0; i < s.length; i++) {
    hash = ((hash << 5) + hash) + (unsigned char)s.data[i];
  }

  return hash;
}

[[nodiscard]] static inline StringView sv_advance(StringView s, size_t n) {
  if (n > s.length) {
    n = s.length;
  }
  return (StringView){.data = s.data + n, .length = s.length - n};
}

static inline bool sv_is_null(StringView s) { return s.data == nullptr; }

static inline bool sv_is_empty(StringView s) { return s.length == 0; }
