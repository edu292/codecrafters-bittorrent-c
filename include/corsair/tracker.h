#pragma once

#include <corsair/peer.h>
#include <corsair/torrent.h>

bool tracker_get_peers(Torrent *t, PeerVector *out);
