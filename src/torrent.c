#include "torrent.h"
#include "bencode.h"
#include <fcntl.h>
#include <openssl/sha.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

bool torrent_info_init(TorrentInfo *ti, const BencodeDict *dict) {
  if (!bencode_dict_get_int(dict, "length", &ti->length) || ti->length <= 0) {
    fprintf(stderr, "Error: 'info.length' missing or invalid\n");
    return false;
  }

  int64_t piece_len = 0;
  if (!bencode_dict_get_int(dict, "piece length", &piece_len) ||
      piece_len <= 0) {
    fprintf(stderr, "Error: 'info.piece length' missing or invalid\n");
    return false;
  }
  ti->piece_length = (size_t)piece_len;
  ti->piece_count = 1 + ((size_t)(ti->length - 1) / ti->piece_length);

  size_t expected_pieces_len = sizeof(Sha1Hash) * ti->piece_count;
  BencodeString pieces;
  if (!bencode_dict_get_str(dict, "pieces", &pieces) ||
      pieces.length != expected_pieces_len) {
    fprintf(stderr, "Error: 'info.pieces' missing or length mismatch\n");
    return false;
  }

  ti->pieces = malloc(expected_pieces_len);
  if (ti->pieces == nullptr) {
    return false;
  }

  memcpy(ti->pieces, pieces.data, expected_pieces_len);
  return true;
}

void torrent_info_destroy(TorrentInfo *ti) { free(ti->pieces); }

size_t torrent_info_piece_length(const TorrentInfo *ti, size_t index) {
  if (index >= ti->piece_count) {
    return 0;
  }

  if (index == ti->piece_count - 1) {
    return (size_t)ti->length - (ti->piece_length * (ti->piece_count - 1));
  }

  return ti->piece_length;
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

  const BencodeDict *root = nullptr;
  if (!bencode_get_dict(&bencode, &root)) {
    fprintf(stderr, "Error: Torrent root is not a dictionary\n");
    goto cleanup_bencode;
  }

  BencodeString announce;
  if (!bencode_dict_get_str(root, "announce", &announce)) {
    fprintf(stderr, "Error: Missing or invalid 'announce' string\n");
    goto cleanup_bencode;
  }

  t->tracker_url = strndup(announce.data, announce.length);
  if (t->tracker_url == nullptr) {
    goto cleanup_bencode;
  }

  Bencode info_raw;
  const BencodeDict *info_dict = nullptr;
  if (!BencodeDict_get(root, bstr("info"), &info_raw) ||
      !bencode_get_dict(&info_raw, &info_dict)) {
    fprintf(stderr, "Error: Missing or invalid 'info' dictionary\n");
    goto cleanup_tracker;
  }

  t->info = malloc(sizeof(TorrentInfo));
  if (t->info == nullptr) {
    goto cleanup_tracker;
  }

  if (!torrent_info_init(t->info, info_dict)) {
    goto cleanup_info;
  }

  SHA1((const unsigned char *)info_raw.raw.data, info_raw.raw.length,
       (unsigned char *)&t->info_hash);
  success = true;
  goto cleanup_bencode;

cleanup_info:
  torrent_info_destroy(t->info);
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
    torrent_info_destroy(t->info);
    free(t->info);
    t->info = nullptr;
  }
}
