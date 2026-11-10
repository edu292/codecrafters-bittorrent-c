#include "bencode_internal.h"
#include "vector.h"
#include <corsair/bencode.h>
#include <corsair/types.h>
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

static inline bool CharVector_extend_quoted(CharVector *v, StringView s) {
  return CharVector_push(v, '"') && CharVector_extend(v, s.data, s.length) &&
         CharVector_push(v, '"');
}

StringView parse_bencode(StringView in, Bencode *out) {
  if (sv_is_empty(in) || sv_is_null(in)) {
    return SV_NULL;
  }

  const char *start = in.data;
  if (isdigit(in.data[0])) {
    const char *colon = memchr(in.data, ':', in.length);
    if (colon == nullptr) {
      goto fail;
    }

    char *endptr = nullptr;
    size_t str_len = strtoumax(in.data, &endptr, 10);
    if (endptr != colon) {
      goto fail;
    }

    size_t header_len = (size_t)(colon + 1 - in.data);
    size_t bencode_len = header_len + str_len;
    if (in.length < bencode_len) {
      goto fail;
    }

    out->type = BENCODE_STRING;
    out->value.string = sv_build(colon + 1, str_len);
    in = sv_advance(in, bencode_len);
  } else {
    switch (in.data[0]) {
    case 'i':
      in = sv_advance(in, 1);
      const char *end = memchr(in.data, 'e', in.length);
      if (end == nullptr) {
        goto fail;
      }

      char *endptr = nullptr;
      int64_t val = strtoimax(in.data, &endptr, 10);
      if (endptr != end) {
        goto fail;
      }

      out->type = BENCODE_INTEGER;
      out->value.integer = val;

      in = sv_advance(in, (size_t)(end + 1 - in.data));
      break;

    case 'l':
      out->type = BENCODE_LIST;
      out->value.list = malloc(sizeof(BencodeList));
      if (out->value.list == nullptr) {
        goto fail;
      }

      if (!BencodeList_init(out->value.list, 16)) {
        free(out->value.list);
        out->value.list = nullptr;
        goto fail;
      }

      in = sv_advance(in, 1);

      while (in.length > 0 && in.data[0] != 'e') {
        Bencode elem;
        in = parse_bencode(in, &elem);

        if (sv_is_null(in) || !BencodeList_push(out->value.list, elem)) {
          goto fail;
        }
      }

      if (in.length == 0) {
        goto fail;
      }

      in = sv_advance(in, 1);
      break;

    case 'd':
      out->type = BENCODE_DICT;
      out->value.dict = malloc(sizeof(BencodeDict));
      if (out->value.dict == nullptr) {
        goto fail;
      }

      if (!BencodeDict_init(out->value.dict, 0)) {
        free(out->value.dict);
        out->value.dict = nullptr;
        goto fail;
      }

      in = sv_advance(in, 1);

      while (in.length > 0 && in.data[0] != 'e') {
        Bencode key;
        Bencode value;
        in = parse_bencode(in, &key);
        if (sv_is_null(in) || key.type != BENCODE_STRING) {
          goto fail;
        }

        in = parse_bencode(in, &value);
        if (sv_is_null(in)) {
          goto fail;
        }

        if (!BencodeDict_set(out->value.dict, key.value.string, value)) {
          bencode_destroy(&value);
          goto fail;
        }
      }

      if (in.length == 0) {
        goto fail;
      }

      in = sv_advance(in, 1);
      break;
    default:
      goto fail;
    }
  }

  out->raw = sv_build(start, (size_t)(in.data - start));
  return in;

fail:
  bencode_destroy(out);
  return SV_NULL;
}

bool bencode_init(Bencode *b, StringView encoded) {
  return !sv_is_null(parse_bencode(encoded, b));
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

    bool ok = true;
    for (size_t i = 0; i < b->value.dict->count; i++) {
      const BencodeDict_entry *entry = entries[i];
      if (!CharVector_extend_quoted(v, entry->key) ||
          !CharVector_push(v, ':') || !bencode_to_vec(&entry->value, v) ||
          (i != b->value.dict->count - 1 && !CharVector_push(v, ','))) {
        ok = false;
        break;
      }
    }

    free(entries);
    if (!ok) {
      return false;
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

bool bencode_get_int(const Bencode *b, int64_t *out) {
  if (b->type != BENCODE_INTEGER) {
    return false;
  }

  *out = b->value.integer;
  return true;
}

bool bencode_get_str(const Bencode *b, StringView *out) {
  if (b->type != BENCODE_STRING) {
    return false;
  }

  *out = b->value.string;
  return true;
}

bool bencode_get_dict(const Bencode *b, const BencodeDict **out) {
  if (b->type != BENCODE_DICT) {
    return false;
  }

  *out = b->value.dict;
  return true;
}

bool bencode_get_list(const Bencode *b, const BencodeList **out) {
  if (b->type != BENCODE_LIST) {
    return false;
  }

  *out = b->value.list;
  return true;
}

bool bencode_dict_get_int(const BencodeDict *dict, const char *key,
                          int64_t *out) {
  Bencode val;
  return BencodeDict_get(dict, sv_from_cstr(key), &val) &&
         bencode_get_int(&val, out);
}

bool bencode_dict_get_str(const BencodeDict *dict, const char *key,
                          StringView *out) {
  Bencode val;
  return BencodeDict_get(dict, sv_from_cstr(key), &val) &&
         bencode_get_str(&val, out);
}

bool bencode_dict_get_dict(const BencodeDict *dict, const char *key,
                           const BencodeDict **out) {
  Bencode val;
  return BencodeDict_get(dict, sv_from_cstr(key), &val) &&
         bencode_get_dict(&val, out);
}
