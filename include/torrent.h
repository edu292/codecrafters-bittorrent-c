#pragma once
#include <stddef.h>
#include <stdint.h>

constexpr size_t SHA1_LEN = 20;

typedef struct {
  uint8_t bytes[SHA1_LEN];
} Sha1Hash;

typedef struct {
  char *name;
  int64_t length;
  size_t piece_length;
  size_t piece_count;
  Sha1Hash *pieces;
} TorrentInfo;

typedef struct {
  char *tracker_url;
  Sha1Hash info_hash;
  TorrentInfo *info;
} Torrent;

bool torrent_from_file(Torrent *t, const char *filepath);
bool torrent_from_magnet(const char *magnet_uri, Torrent *out);
void torrent_destroy(Torrent *t);
