#include "torrent.h"
#include "bencode.h"
#include <fcntl.h>
#include <openssl/sha.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

bool torrent_info_init(TorrentInfo *ti, const BencodeDict *dict) {
  Bencode len;
  if (!BencodeDict_get(dict, bstr("length"), &len) ||
      len.type != BENCODE_INTEGER) {
    fprintf(stderr, "Error: 'info.length' missing or not an integer\n");
    return false;
  }

  if (len.value.integer <= 0) {
    fprintf(stderr, "Error: 'info.length' must be bigger than zero\n");
    return false;
  }

  ti->length = len.value.integer;

  Bencode piece_length;
  if (!BencodeDict_get(dict, bstr("piece length"), &piece_length) ||
      piece_length.type != BENCODE_INTEGER) {
    fprintf(stderr, "missng piece length\n");
    return false;
  }
  ti->piece_length = (size_t)piece_length.value.integer;
  ti->piece_count = 1 + ((ti->length - 1) / ti->piece_length);
  size_t pieces_len = sizeof(Sha1Hash) * ti->piece_count;

  Bencode pieces;
  if (!BencodeDict_get(dict, bstr("pieces"), &pieces) ||
      pieces.type != BENCODE_STRING ||
      pieces.value.string.length != pieces_len) {
    fprintf(stderr, "missng pieces\n");
    return false;
  }

  ti->pieces = malloc(pieces_len);
  if (ti->pieces == nullptr) {
    perror("malloc");
    return false;
  }

  memcpy(ti->pieces, pieces.value.string.data, pieces_len);
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

  char *data = mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
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
  if (!BencodeDict_get(bencode.value.dict, bstr("announce"), &announce_val) ||
      announce_val.type != BENCODE_STRING) {
    fprintf(stderr, "Error: Missing or invalid 'announce' string\n");
    goto cleanup_bencode;
  }

  t->tracker_url = strdup(announce_val.value.string.data);
  if (!t->tracker_url) {
    goto cleanup_bencode;
  }

  Bencode info_val;
  if (!BencodeDict_get(bencode.value.dict, bstr("info"), &info_val) ||
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

  SHA1((const unsigned char *)info_val.raw.data, info_val.raw.length,
       (unsigned char *)&t->info_hash);
  success = true;
  goto cleanup_bencode;

cleanup_info:
  free(t->info);
  t->info = nullptr;

cleanup_tracker:
  free(t->tracker_url);
  t->tracker_url = nullptr;

cleanup_bencode:
  bencode_destroy(&bencode);

cleanup_mmap:
  munmap(data, sb.st_size);

  return success;
}

void torrent_destroy(Torrent *t) {
  free(t->tracker_url);
  t->tracker_url = nullptr;
  if (t->info) {
    free(t->info);
    t->info = nullptr;
  }
}
