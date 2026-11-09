#pragma once

#include <corsair/torrent.h>
#include <corsair/types.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  char ip[16];
  uint16_t port;
} Peer;

void peer_from_bytes(Peer *p, const uint8_t *src);
bool peer_from_str(Peer *p, const char *src);

typedef enum {
  PEER_DL_OK,
  PEER_DL_ERR_SOCKET,
  PEER_DL_ERR_CHOKED,
  PEER_DL_ERR_CORRUPTED,
  PEER_DL_ERR_MISSING_PIECE,
  PEER_DL_ERR_INVALID_ARGS,
} PeerDownloadResult;

typedef struct {
  PeerId peer_id;
  size_t bitfield_length;
  uint8_t *bitfield;
  uint8_t *extensions_table;
  uint8_t state;
  int fd;
} PeerConnection;

bool peer_connect(const Peer *p, const Torrent *t, PeerConnection *pc);
void peer_disconnect(PeerConnection *pc);
PeerDownloadResult peer_download_piece(PeerConnection *pc,
                                       const TorrentInfo *ti,
                                       size_t piece_index, uint8_t *out);
bool peer_download_piece_to_file(PeerConnection *pc, const TorrentInfo *ti,
                                 size_t piece_index, const char *out_path);

typedef struct PeerVector {
  Peer *data;
  size_t capacity;
  size_t count;
} PeerVector;

bool peer_vector_init(PeerVector *pv, size_t capacity);
void peer_vector_destroy(PeerVector *pv);
