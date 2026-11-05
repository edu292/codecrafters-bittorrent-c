#include "vector.h"
#include <stdint.h>

typedef struct Bencode Bencode;

VECTOR_TYPE(Bencode, BencodeList)

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
    long long integer;
    BencodeList list;
  } value;
} Bencode;

VECTOR_METHODS(Bencode, BencodeList)

bool bencode_init(Bencode *b, const char *encoded);
char *bencode_to_str(Bencode *b);
void bencode_destroy(Bencode *b);
