#include "encoding.h"
#include <corsair/magnet.h>
#include <curl/curl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

bool starts_with(const char *str, const char *prefix) {
  while (*prefix != '\0') {
    if (*prefix++ != *str++) {
      return false;
    }
  }

  return true;
}

bool magnet_parse(Magnet *m, char *uri) {
  static const char magnet_prefix[] = "magnet:?";
  if (!starts_with(uri, magnet_prefix)) {
    return false;
  }

  char *cursor = uri + sizeof(magnet_prefix) - 1;

  bool has_valid_info_hash = false;
  char *key_value;
  while ((key_value = strsep(&cursor, "&")) != nullptr) {
    if (*key_value == '\0') {
      continue;
    }

    char *eq = strchr(key_value, '=');
    if (eq == nullptr) {
      continue;
    }

    *eq = '\0';
    char *key = key_value;
    char *value = eq + 1;
    if (*value == '\0') {
      continue;
    }

    if (strcmp(key, "xt") == 0) {
      static const char urn_prefix[] = "urn:btih:";
      if (!starts_with(value, urn_prefix)) {
        continue;
      }

      value += sizeof(urn_prefix) - 1;
      size_t len = strlen(value);
      if (len == 40 && hex_to_bytes(m->btih.bytes, value, 40)) {
        has_valid_info_hash = true;
      } else if (len == 32 && base32_to_bytes(m->btih.bytes, value, 32)) {
        has_valid_info_hash = true;
      }
    } else if (strcmp(key_value, "dn") == 0) {
      url_decode(value);
      m->display_name = value;
    } else if (strcmp(key_value, "tr") == 0) {
      url_decode(value);
      m->tracker = value;
    }
  }

  return has_valid_info_hash;
}
