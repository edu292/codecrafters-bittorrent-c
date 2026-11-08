#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static const char hex_chars[] = "0123456789abcdef";

static inline void print_hex(const uint8_t *bytes, size_t len) {
  for (size_t i = 0; i < len; i++) {
    printf("%02x", bytes[i]);
  }
}

static inline char *bytes_to_hex(char *dst, const uint8_t *bytes, size_t len) {
  for (size_t i = 0; i < len; i++) {
    *dst++ = hex_chars[(bytes[i] >> 4) & 0x0F];
    *dst++ = hex_chars[bytes[i] & 0x0F];
  }

  return dst;
}

static inline char *bytes_to_percent_hex(char *dst, const uint8_t *bytes,
                                         size_t len) {
  for (size_t i = 0; i < len; i++) {
    *dst++ = '%';
    *dst++ = hex_chars[(bytes[i] >> 4) & 0x0F];
    *dst++ = hex_chars[bytes[i] & 0x0F];
  }

  return dst;
}
