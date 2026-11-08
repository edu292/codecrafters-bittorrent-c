#pragma once

#include "torrent.h"
#include <stdint.h>

typedef struct {
  Sha1Hash btih;
  char *display_name;
  char *tracker;
  uint64_t exact_length;
} Magnet;

bool magnet_parse(Magnet *m, char *uri);
void magnet_destroy(Magnet *m);
