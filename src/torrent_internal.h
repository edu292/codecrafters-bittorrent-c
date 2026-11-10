#pragma once

#include <corsair/torrent.h>

bool torrent_info_init(TorrentInfo *ti, const BencodeDict *dict);
void torrent_info_destroy(TorrentInfo *ti);
bool torrent_load_metadata(Torrent *t, StringView raw_metadata);
size_t torrent_info_piece_length(const TorrentInfo *ti, size_t index);
