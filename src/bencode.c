#include "bencode.h"
#include "vector.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>

DEFINE_VECTOR(char, CharVector)

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

    if (!BencodeList_init(&out->value.list, 16)) {
      return nullptr;
    }

    while (*encoded != 'e') {
      Bencode elem;
      encoded = parse_bencode(encoded, &elem);
      if (encoded == nullptr) {
        bencode_destroy(out);
        return nullptr;
      }

      if (!BencodeList_push(&out->value.list, elem)) {
        bencode_destroy(&elem);
        bencode_destroy(out);
        return nullptr;
      }
    }
    return encoded + 1;
  case 'd':
    return nullptr;
  }

  return nullptr;
}

bool bencode_init(Bencode *b, const char *encoded) {
  return parse_bencode(encoded, b) != nullptr;
}

static bool bencode_to_vec(const Bencode *b, CharVector *v) {
  switch (b->type) {
  case BENCODE_STRING: {
    if (!CharVector_push(v, '"')) {
      return false;
    }

    if (!CharVector_extend(v, b->value.string, strlen(b->value.string))) {
      return false;
    }

    return CharVector_push(v, '"');
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

    for (size_t i = 0; i < b->value.list.count; i++) {
      if (!bencode_to_vec(&b->value.list.data[i], v)) {
        return false;
      }

      if (i != b->value.list.count - 1) {
        if (!CharVector_push(v, ',')) {
          return false;
        }
      }
    }

    return CharVector_push(v, ']');
  }
  case BENCODE_DICT:
    return false;
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
    b->value.string = nullptr;
    break;

  case BENCODE_LIST:
    for (size_t i = 0; i < b->value.list.count; i++) {
      bencode_destroy(&b->value.list.data[i]);
    }

    BencodeList_destroy(&b->value.list);
    break;
  case BENCODE_INTEGER:
  case BENCODE_DICT:
    break;
  }
}
