#pragma once
#include "torrent.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static inline void print_hex(const Sha1Hash *hash) {
  for (size_t i = 0; i < sizeof(Sha1Hash); i++) {
    printf("%02x", hash->bytes[i]);
  }
}

static inline void bytes_to_hex(const Sha1Hash *hash, char *out) {
  static const char hex_chars[] = "0123456789abcdef";
  size_t len = sizeof(Sha1Hash);
  for (size_t i = 0; i < len; i++) {
    out[i * 2] = hex_chars[(hash->bytes[i] >> 4) & 0x0F];
    out[i * 2 + 1] = hex_chars[hash->bytes[i] & 0x0F];
  }

  out[len * 2] = '\0';
}
