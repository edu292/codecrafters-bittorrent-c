#include "torrent.h"
#include "bencode.h"
#include <fcntl.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

bool torrent_info_init(TorrentInfo *ti, const BencodeDict *dict) {
  Bencode len;
  if (!BencodeDict_get(dict, "length", &len) || len.type != BENCODE_INTEGER) {
    fprintf(stderr, "Error: 'info.length' missing or not an integer\n");
    return false;
  }

  if (len.value.integer < 0) {
    fprintf(stderr, "Error: 'info.length' cannot be negative\n");
    return false;
  }

  ti->length = len.value.integer;
  return true;
}

bool torrent_from_file(Torrent *t, const char *filepath) {
  int fd = open(filepath, O_RDONLY);
  if (fd < 0) {
    perror("open");
    return false;
  }

  struct stat sb;
  if (fstat(fd, &sb) < 0 || sb.st_size == 0) {
    fprintf(stderr, "Error: Invalid file size for %s\n", filepath);
    close(fd);
    return false;
  }

  char *data = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);

  if (data == MAP_FAILED) {
    perror("mmap");
    return false;
  }

  bool success = false;
  Bencode bencode;

  if (!bencode_init(&bencode, data)) {
    fprintf(stderr, "Error: Failed to parse bencode from %s\n", filepath);
    goto cleanup_mmap;
  }

  if (bencode.type != BENCODE_DICT) {
    fprintf(stderr, "Error: Torrent root is not a dictionary\n");
    goto cleanup_bencode;
  }

  Bencode announce_val;
  if (!BencodeDict_get(bencode.value.dict, "announce", &announce_val) ||
      announce_val.type != BENCODE_STRING) {
    fprintf(stderr, "Error: Missing or invalid 'announce' string\n");
    goto cleanup_bencode;
  }

  t->tracker_url = strdup(announce_val.value.string);
  if (!t->tracker_url) {
    goto cleanup_bencode;
  }

  Bencode info_val;
  if (!BencodeDict_get(bencode.value.dict, "info", &info_val) ||
      info_val.type != BENCODE_DICT) {
    fprintf(stderr, "Error: Missing or invalid 'info' dictionary\n");
    goto cleanup_tracker;
  }

  t->info = malloc(sizeof(TorrentInfo));
  if (t->info == nullptr) {
    goto cleanup_tracker;
  }

  if (!torrent_info_init(t->info, info_val.value.dict)) {
    goto cleanup_info;
  }

  SHA1((const unsigned char *)info_val.raw, info_val.raw_len, t->info_hash);
  success = true;
  goto cleanup_bencode;

cleanup_info:
  free(t->info);
  t->info = NULL;

cleanup_tracker:
  free(t->tracker_url);
  t->tracker_url = NULL;

cleanup_bencode:
  bencode_destroy(&bencode);

cleanup_mmap:
  munmap(data, sb.st_size);

  return success;
}

void torrent_destroy(Torrent *t) {
  free(t->tracker_url);
  t->tracker_url = NULL;
  if (t->info) {
    free(t->info);
    t->info = NULL;
  }
}
