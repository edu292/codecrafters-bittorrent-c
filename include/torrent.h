#include <stddef.h>
#include <stdint.h>

typedef struct {
  char *name;
  int64_t length;
  size_t piece_length;
  size_t num_pieces;
  uint8_t *pieces;
} TorrentInfo;

typedef struct {
  char *tracker_url;
  uint8_t info_hash[20];
  TorrentInfo *info;
} Torrent;

bool torrent_from_file(Torrent *t, const char *filepath);
bool torrent_from_magnet(const char *magnet_uri, Torrent *out);
void torrent_destroy(Torrent *t);
