#pragma once
#include "torrent.h"
#include "vector.h"
#include <stddef.h>
#include <stdint.h>

constexpr size_t PEER_BLOCK_SIZE = 16ULL * 1024;

typedef struct {
  char ip[16];
  uint16_t port;
} Peer;

void peer_from_bytes(Peer *p, const uint8_t *src);
bool peer_from_str(Peer *p, const char *src);

typedef struct __attribute__((packed)) {
  uint8_t pstrlen;
  char pstr[19];
  uint8_t reserved[8];
  Sha1Hash info_hash;
  uint8_t peer_id[20];
} PeerHandshake;

typedef enum : uint8_t {
  PEER_MSG_CHOKE = 0,
  PEER_MSG_UNCHOKE = 1,
  PEER_MSG_INTERESTED = 2,
  PEER_MSG_NOT_INTERESTED = 3,
  PEER_MSG_HAVE = 4,
  PEER_MSG_BITFIELD = 5,
  PEER_MSG_REQUEST = 6,
  PEER_MSG_PIECE = 7,
  PEER_MSG_CANCEL = 8,
} PeerMessageId;

typedef struct {
  uint32_t length;
  PeerMessageId id;
  void *payload;
} PeerMessage;

typedef struct {
  uint32_t index;
  uint32_t begin;
  uint32_t length;
} PeerMsgRequestPayload;

typedef struct {
  uint32_t index;
  uint32_t begin;
  uint8_t block[];
} PeerMsgPiecePayload;

typedef struct {
  int fd;
  uint8_t peer_id[20];
} PeerSession;

bool peer_session_init(PeerSession *ps, const Peer *p,
                       const Sha1Hash *info_hash);
void peer_session_destroy(PeerSession *ps);
bool peer_session_download_piece(PeerSession *ps, const TorrentInfo *ti,
                                 size_t index, uint8_t *out);
bool torrent_download_piece_to_file(PeerSession *ps, const TorrentInfo *ti,
                                    size_t piece_index, const char *out_path);

    DEFINE_VECTOR(Peer, PeerVector)
