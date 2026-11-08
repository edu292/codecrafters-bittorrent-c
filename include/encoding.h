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

static inline int hex_val(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }

  return -1;
}

static inline bool hex_to_bytes(uint8_t *dst, const char *hex, size_t hex_len) {
  if (hex_len % 2 != 0) {
    return false;
  }

  for (size_t i = 0; i < hex_len; i += 2) {
    int hi = hex_val(hex[i]);
    int lo = hex_val(hex[i + 1]);

    if (hi < 0 || lo < 0) {
      return false;
    }

    *dst++ = (uint8_t)((hi << 4) | lo);
  }

  return true;
}

static inline int base32_val(char c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a';
  }
  if (c >= '2' && c <= '7') {
    return c - '2' + 26;
  }

  return -1;
}

static inline bool base32_to_bytes(uint8_t *dst, const char *src,
                                   size_t base32_len) {
  if (base32_len % 8 != 0) {
    return false;
  }

  for (size_t i = 0; i < base32_len; i += 8) {
    uint64_t buf = 0;
    for (size_t j = 0; j < 8; j++) {
      int val = base32_val(src[i + j]);
      if (val < 0) {
        return false;
      }

      buf = (buf << 5) | (uint32_t)val;
    }

    *dst++ = (uint8_t)(buf >> 32);
    *dst++ = (uint8_t)(buf >> 24);
    *dst++ = (uint8_t)(buf >> 16);
    *dst++ = (uint8_t)(buf >> 8);
    *dst++ = (uint8_t)buf;
  }

  return true;
}

static inline size_t url_decode(char *str) {
  char *src = str;
  char *dst = str;

  while (*src) {
    uint8_t byte;
    if (*src == '%' && src[1] && src[2] && hex_to_bytes(&byte, src + 1, 2)) {
      if (byte != 0) {
        *dst++ = (char)byte;
      }

      src += 3;
    } else if (*src == '+') {
      *dst++ = ' ';
      src++;
    } else {
      *dst++ = *src++;
    }
  }

  *dst = '\0';
  return (size_t)(dst - str);
}
