#pragma once
#include "hashmap.h"
#include "vector.h"
#include <stddef.h>
#include <stdint.h>

typedef struct BencodeDict BencodeDict;
typedef struct BencodeList BencodeList;

typedef enum : uint8_t {
  BENCODE_STRING,
  BENCODE_INTEGER,
  BENCODE_LIST,
  BENCODE_DICT,
} BencodeType;

typedef struct {
  const char *data;
  size_t length;
} BencodeString;

static inline BencodeString bstr(const char *s) {
  return (BencodeString){.data = s, .length = strlen(s)};
}

typedef struct Bencode {
  BencodeType type;
  BencodeString raw;
  union {
    BencodeString string;
    int64_t integer;
    BencodeList *list;
    BencodeDict *dict;
  } value;
} Bencode;

bool bencode_init(Bencode *b, const char *encoded);
char *bencode_to_str(Bencode *b);
void bencode_destroy(Bencode *b);

DEFINE_VECTOR(Bencode, BencodeList)

static inline size_t hash_str(BencodeString s) {
  size_t hash = 5381;
  for (size_t i = 0; i < s.length; i++) {
    hash = ((hash << 5) + hash) + (unsigned char)s.data[i];
  }
  return hash;
}

static inline bool eq_str(BencodeString a, BencodeString b) {
  if (a.length != b.length) {
    return false;
  }
  return memcmp(a.data, b.data, a.length) == 0;
}

DEFINE_HASHMAP(BencodeString, Bencode, BencodeDict, hash_str, eq_str)
