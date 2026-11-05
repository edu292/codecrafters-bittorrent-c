#include "bencode.h"
#include "vector.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

DEFINE_VECTOR(char, CharVector)

static int cmp_bencode_entries(const void *a, const void *b) {
  const BencodeDict_entry *entry_a = *(const BencodeDict_entry *const *)a;
  const BencodeDict_entry *entry_b = *(const BencodeDict_entry *const *)b;
  return strcmp(entry_a->key, entry_b->key);
}

static inline bool CharVector_extend_quoted(CharVector *v, const char *str) {
  size_t len = strlen(str);
  return CharVector_push(v, '"') && CharVector_extend(v, str, len) &&
         CharVector_push(v, '"');
}

const char *parse_bencode(const char *encoded, Bencode *out) {
  if (encoded == nullptr || *encoded == '\0') {
    return nullptr;
  }

  if (isdigit(*encoded)) {
    const char *colon = strchr(encoded, ':');
    if (colon == nullptr) {
      return nullptr;
    }

    int len = atoi(encoded);
    const char *data = colon + 1;

    char *str = malloc(len + 1);
    memcpy(str, data, len);
    str[len] = '\0';

    out->type = BENCODE_STRING;
    out->value.string = str;
    return data + len;
  }

  switch (encoded[0]) {
  case 'i':
    const char *end = strchr(encoded + 1, 'e');
    if (end == nullptr) {
      return nullptr;
    }

    out->type = BENCODE_INTEGER;
    out->value.integer = atoll(encoded + 1);
    return end + 1;
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
      if (encoded == nullptr) {
        goto fail;
      }

      if (!BencodeList_push(out->value.list, elem)) {
        return nullptr;
        goto fail;
      }
    }
    return encoded + 1;
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
      if (encoded == nullptr) {
        goto fail;
      }

      if (key.type != BENCODE_STRING) {
        bencode_destroy(&key);
        goto fail;
      }

      encoded = parse_bencode(encoded, &value);
      if (encoded == nullptr) {
        bencode_destroy(&key);
        goto fail;
      }

      if (!BencodeDict_set(out->value.dict, key.value.string, value)) {
        bencode_destroy(&key);
        bencode_destroy(&value);
        goto fail;
      }
    }

    return encoded + 1;
  default:
    return nullptr;
  }

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
    int len = snprintf(num_buf, sizeof(num_buf), "%ld", b->value.integer);
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
      CharVector_extend_quoted(v, entry->key);
      CharVector_push(v, ':');
      if (!bencode_to_vec(&entry->value, v)) {
        return false;
      }

      if (i != b->value.dict->count - 1) {
        if (!CharVector_push(v, ',')) {
          return false;
        }
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
  case BENCODE_STRING:
    free(b->value.string);
    break;

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
      free(entry->key);
      bencode_destroy((Bencode *)&entry->value);
    }

    BencodeDict_destroy(b->value.dict);
    free(b->value.dict);
    break;
  case BENCODE_INTEGER:
    break;
  }
}
