#pragma once
#include "torrent.h"
#include "vector.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
  char ip[16];
  uint16_t port;
} Peer;

void peer_from_bytes(Peer *p, const uint8_t *src);
bool peer_from_str(Peer *p, const char *src);

typedef enum {
  PEER_DL_OK = 0,
  PEER_DL_MISSING_PIECE,
  PEER_DL_CHOKED,
  PEER_DL_INVALID_ARGS,
  PEER_DL_CORRUPTED,
  PEER_DL_ERR_SOCKET,
} PeerDownloadStatus;

typedef struct {
  uint8_t peer_id[20];
  size_t bitfield_length;
  uint8_t *bitfield;
  int fd;
  bool is_choked;
  bool is_interested;
} PeerConnection;

bool peer_connect(const Peer *p, const Torrent *t, PeerConnection *pc);
void peer_disconnect(PeerConnection *pc);
PeerDownloadStatus peer_download_piece(PeerConnection *pc,
                                       const TorrentInfo *ti,
                                       size_t piece_index, uint8_t *out);
bool peer_download_piece_to_file(PeerConnection *pc, const TorrentInfo *ti,
                                 size_t piece_index, const char *out_path);

DEFINE_VECTOR(Peer, PeerVector)
