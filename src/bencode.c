#include "bencode.h"
#include "vector.h"
#include <ctype.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

DEFINE_VECTOR(char, CharVector)

static int cmp_bencode_entries(const void *a, const void *b) {
  const BencodeDict_entry *entry_a = *(const BencodeDict_entry *const *)a;
  const BencodeDict_entry *entry_b = *(const BencodeDict_entry *const *)b;

  size_t len_a = entry_a->key.length;
  size_t len_b = entry_b->key.length;
  size_t min_len = len_a < len_b ? len_a : len_b;

  int cmp = memcmp(entry_a->key.data, entry_b->key.data, min_len);
  if (cmp != 0) {
    return cmp;
  }

  return (len_a > len_b) - (len_a < len_b);
}

static inline bool CharVector_extend_quoted(CharVector *v, BencodeString s) {
  return CharVector_push(v, '"') && CharVector_extend(v, s.data, s.length) &&
         CharVector_push(v, '"');
}

const char *parse_bencode(const char *encoded, Bencode *out) {
  if (*encoded == '\0') {
    return nullptr;
  }

  const char *start = encoded;
  const char *next = nullptr;

  if (isdigit(*encoded)) {
    const char *colon = strchr(encoded, ':');
    if (colon == nullptr) {
      return nullptr;
    }

    size_t len = strtoumax(encoded, nullptr, 10);
    const char *data = colon + 1;

    out->type = BENCODE_STRING;
    out->value.string.data = data;
    out->value.string.length = len;
    next = data + len;
  } else {
    switch (encoded[0]) {
    case 'i':
      const char *end = strchr(encoded + 1, 'e');
      if (end == nullptr) {
        return nullptr;
      }

      out->type = BENCODE_INTEGER;
      out->value.integer = strtoimax(encoded + 1, nullptr, 10);
      next = end + 1;
      break;

    case 'l':
      encoded++;
      out->type = BENCODE_LIST;
      out->value.list = malloc(sizeof(BencodeList));
      if (out->value.list == nullptr) {
        return nullptr;
      }

      if (!BencodeList_init(out->value.list, 16)) {
        free(out->value.list);
        return nullptr;
      }

      while (*encoded != 'e') {
        Bencode elem;
        encoded = parse_bencode(encoded, &elem);
        if (encoded == nullptr || !BencodeList_push(out->value.list, elem)) {
          goto fail;
        }
      }
      next = encoded + 1;
      break;

    case 'd':
      encoded++;
      out->type = BENCODE_DICT;
      out->value.dict = malloc(sizeof(BencodeDict));
      if (out->value.dict == nullptr) {
        return nullptr;
      }

      if (!BencodeDict_init(out->value.dict, 0)) {
        free(out->value.dict);
        return nullptr;
      }

      while (*encoded != 'e') {
        Bencode key;
        Bencode value;
        encoded = parse_bencode(encoded, &key);
        if (encoded == nullptr || key.type != BENCODE_STRING) {
          goto fail;
        }

        encoded = parse_bencode(encoded, &value);
        if (encoded == nullptr) {
          goto fail;
        }

        if (!BencodeDict_set(out->value.dict, key.value.string, value)) {
          bencode_destroy(&value);
          goto fail;
        }
      }

      next = encoded + 1;
      break;
    default:
      return nullptr;
    }
  }

  out->raw.data = start;
  out->raw.length = (size_t)(next - start);
  return next;

fail:
  bencode_destroy(out);
  return nullptr;
}

bool bencode_init(Bencode *b, const char *encoded) {
  return parse_bencode(encoded, b) != nullptr;
}

static bool bencode_to_vec(const Bencode *b, CharVector *v) {
  switch (b->type) {
  case BENCODE_STRING: {
    return CharVector_extend_quoted(v, b->value.string);
  }

  case BENCODE_INTEGER: {
    char num_buf[32];
    int len = snprintf(num_buf, sizeof(num_buf), "%lld", b->value.integer);
    if (len < 0) {
      return false;
    }

    return CharVector_extend(v, num_buf, (size_t)len);
  }

  case BENCODE_LIST: {
    if (!CharVector_push(v, '[')) {
      return false;
    }

    for (size_t i = 0; i < b->value.list->count; i++) {
      if (!bencode_to_vec(&b->value.list->data[i], v)) {
        return false;
      }

      if (i != b->value.list->count - 1) {
        if (!CharVector_push(v, ',')) {
          return false;
        }
      }
    }

    return CharVector_push(v, ']');
  }
  case BENCODE_DICT:
    if (!CharVector_push(v, '{')) {
      return false;
    }

    const BencodeDict_entry **entries = BencodeDict_get_entries(b->value.dict);
    qsort(entries, b->value.dict->count, sizeof(BencodeDict_entry *),
          cmp_bencode_entries);

    for (size_t i = 0; i < b->value.dict->count; i++) {
      const BencodeDict_entry *entry = entries[i];
      if (!CharVector_extend_quoted(v, entry->key) ||
          !CharVector_push(v, ':') || !bencode_to_vec(&entry->value, v)) {
        return false;
      }

      if (i != b->value.dict->count - 1 && !CharVector_push(v, ',')) {
        return false;
      }
    }

    return CharVector_push(v, '}');
  }

  return false;
}

char *bencode_to_str(Bencode *b) {
  CharVector vec;
  if (!CharVector_init(&vec, 128)) {
    return nullptr;
  }

  if (!bencode_to_vec(b, &vec) || !CharVector_push(&vec, '\0')) {
    CharVector_destroy(&vec);
    return nullptr;
  }

  return vec.data;
}

void bencode_destroy(Bencode *b) {
  switch (b->type) {
  case BENCODE_LIST:
    for (size_t i = 0; i < b->value.list->count; i++) {
      bencode_destroy(&b->value.list->data[i]);
    }

    BencodeList_destroy(b->value.list);
    free(b->value.list);
    break;
  case BENCODE_DICT:
    const BencodeDict_entry **entries = BencodeDict_get_entries(b->value.dict);
    for (size_t i = 0; i < b->value.dict->count; i++) {
      const BencodeDict_entry *entry = entries[i];
      bencode_destroy((Bencode *)&entry->value);
    }

    BencodeDict_destroy(b->value.dict);
    free(b->value.dict);
    free(entries);
    break;
  case BENCODE_STRING:
  case BENCODE_INTEGER:
    break;
  }
}
