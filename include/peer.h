#pragma once
#include "torrent.h"
#include "vector.h"
#include <stdint.h>

typedef struct __attribute__((packed)) {
  uint8_t pstrlen;
  char pstr[19];
  uint8_t reserved[8];
  Sha1Hash info_hash;
  uint8_t peer_id[20];
} PeerHandshake;

typedef struct {
  int fd;
  uint8_t peer_id[20];
} PeerSession;

typedef struct {
  char ip[16];
  uint16_t port;
} Peer;

void peer_from_bytes(Peer *p, const uint8_t *src);
bool peer_from_str(Peer *p, const char *src);
bool peer_session_init(PeerSession *ps, const Peer *p,
                       const Sha1Hash *info_hash);

DEFINE_VECTOR(Peer, PeerVector)
