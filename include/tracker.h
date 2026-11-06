#pragma once
#include "peer.h"
#include "torrent.h"

bool tracker_get_peers(Torrent *t, PeerVector *out);
