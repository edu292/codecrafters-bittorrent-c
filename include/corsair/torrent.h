#pragma once

#include <corsair/types.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  char *name;
  int64_t length;
  size_t piece_length;
  size_t piece_count;
  Sha1Hash *pieces;
} TorrentInfo;

size_t torrent_info_piece_length(const TorrentInfo *ti, size_t index);

typedef struct Torrent {
  Sha1Hash info_hash;
  PeerId peer_id;
  char *tracker_url;
  TorrentInfo *info;
} Torrent;

bool torrent_from_file(Torrent *t, const char *path);
bool torrent_from_magnet(Torrent *t, char *uri);
bool torrent_download_to_file(Torrent *t, const char *path);
void torrent_destroy(Torrent *t);
