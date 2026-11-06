#include "peer.h"
#include <stdio.h>

void peer_init(const uint8_t *src, Peer *out) {
  snprintf(out->ip, sizeof(out->ip), "%u.%u.%u.%u", src[0], src[1], src[2],
           src[3]);

  out->port = (uint16_t)((src[4] << 8) | src[5]);
}
