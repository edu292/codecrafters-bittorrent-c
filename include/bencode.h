#include "hashmap.h"
#include "vector.h"
#include <stdint.h>

typedef struct BencodeDict BencodeDict;
typedef struct BencodeList BencodeList;

typedef enum : uint8_t {
  BENCODE_STRING,
  BENCODE_INTEGER,
  BENCODE_LIST,
  BENCODE_DICT,
} BencodeType;

typedef struct Bencode {
  BencodeType type;
  union {
    char *string;
    int64_t integer;
    BencodeList *list;
    BencodeDict *dict;
  } value;
} Bencode;

bool bencode_init(Bencode *b, const char *encoded);
char *bencode_to_str(Bencode *b);
void bencode_destroy(Bencode *b);

DEFINE_VECTOR(Bencode, BencodeList)

static inline size_t hash_str(const char *str) {
  size_t hash = 5381;
  int c;
  while ((c = (unsigned char)*str++)) {
    hash = ((hash << 5) + hash) + c;
  }
  return hash;
}

static inline bool eq_str(const char *a, const char *b) {
  return strcmp(a, b) == 0;
}

DEFINE_HASHMAP(char *, Bencode, BencodeDict, hash_str, eq_str)
