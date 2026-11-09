#pragma once

#include <corsair/types.h>
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

typedef struct Bencode {
  BencodeType type;
  StringView raw;
  union {
    StringView string;
    int64_t integer;
    BencodeList *list;
    BencodeDict *dict;
  } value;
} Bencode;

bool bencode_init(Bencode *b, const char *encoded);
char *bencode_to_str(Bencode *b);
void bencode_destroy(Bencode *b);

bool bencode_get_int(const Bencode *b, int64_t *out);
bool bencode_get_str(const Bencode *b, StringView *out);
bool bencode_get_dict(const Bencode *b, const BencodeDict **out);
bool bencode_get_list(const Bencode *b, const BencodeList **out);

bool bencode_dict_get_int(const BencodeDict *dict, const char *key,
                          int64_t *out);
bool bencode_dict_get_str(const BencodeDict *dict, const char *key,
                          StringView *out);
bool bencode_dict_get_dict(const BencodeDict *dict, const char *key,
                           const BencodeDict **out);
