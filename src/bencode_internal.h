#pragma once

#include "hashmap.h"
#include "vector.h"
#include <corsair/bencode.h>

const char *parse_bencode(const char *encoded, Bencode *out);

DEFINE_VECTOR(Bencode, BencodeList)
DEFINE_HASHMAP(StringView, Bencode, BencodeDict, sv_hash, sv_eq)
