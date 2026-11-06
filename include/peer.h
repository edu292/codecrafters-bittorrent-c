#pragma once
#include "vector.h"
#include <stdint.h>

typedef struct {
  char ip[16];
  uint16_t port;
} Peer;

void peer_init(const uint8_t *src, Peer *out);

DEFINE_VECTOR(Peer, PeerVector)
