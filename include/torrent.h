#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint8_t bytes[20];
} Sha1Hash;

typedef struct {
  char *name;
  int64_t length;
  size_t piece_length;
  size_t piece_count;
  Sha1Hash *pieces;
} TorrentInfo;

size_t torrent_info_piece_length(const TorrentInfo *ti, size_t index);

typedef struct {
  Sha1Hash info_hash;
  uint8_t peer_id[20];
  char *tracker_url;
  TorrentInfo *info;
} Torrent;

bool torrent_from_file(Torrent *t, const char *filepath);
bool torrent_from_magnet(const char *magnet_uri, Torrent *out);
bool torrent_download_to_file(Torrent *torrent, const char *out_path);
void torrent_destroy(Torrent *t);
