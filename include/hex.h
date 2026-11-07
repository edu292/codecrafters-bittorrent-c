#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static inline void print_hex(const uint8_t *bytes, size_t len) {
  for (size_t i = 0; i < len; i++) {
    printf("%02x", bytes[i]);
  }
}

static inline void bytes_to_hex(const uint8_t *bytes, size_t len, char *out) {
  static const char hex_chars[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out[i * 2] = hex_chars[(bytes[i] >> 4) & 0x0F];
    out[i * 2 + 1] = hex_chars[bytes[i] & 0x0F];
  }

  out[len * 2] = '\0';
}
