#pragma once

#include "hashmap.h"
#include "vector.h"
#include <corsair/bencode.h>
#include <corsair/types.h>

StringView parse_bencode(StringView in, Bencode *out);

DEFINE_VECTOR(Bencode, BencodeList)
DEFINE_HASHMAP(StringView, Bencode, BencodeDict, sv_hash, sv_eq)
