#pragma once

#include "vector.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

DEFINE_VECTOR(char, CharVector)

static inline bool bencode_write_dict_begin(CharVector *w) {
  return CharVector_push(w, 'd');
}

static inline bool bencode_write_dict_end(CharVector *w) {
  return CharVector_push(w, 'e');
}

static inline bool bencode_write_list_begin(CharVector *w) {
  return CharVector_push(w, 'l');
}

static inline bool bencode_write_list_end(CharVector *w) {
  return CharVector_push(w, 'e');
}

static inline bool bencode_write_int(CharVector *w, int64_t val) {
  char buf[32];
  int n = snprintf(buf, sizeof(buf), "i%llde", (long long)val);
  return (n > 0) && CharVector_extend(w, buf, (size_t)n);
}

static inline bool bencode_write_str(CharVector *w, const char *str) {
  size_t len = strlen(str);
  char prefix[32];
  int n = snprintf(prefix, sizeof(prefix), "%zu:", len);
  if (n <= 0 || !CharVector_extend(w, prefix, (size_t)n)) {
    return false;
  }
  return CharVector_extend(w, str, len);
}

static inline bool bencode_write_key_int(CharVector *w, const char *key,
                                         int64_t val) {
  return bencode_write_str(w, key) && bencode_write_int(w, val);
}

static inline bool bencode_write_key_str(CharVector *w, const char *key,
                                         const char *val) {
  return bencode_write_str(w, key) && bencode_write_str(w, val);
}
