#include "torrent.h"
#include "bencode.h"
#include "concurent_queue.h"
#include "peer.h"
#include "tracker.h"
#include <fcntl.h>
#include <linux/limits.h>
#include <openssl/sha.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <threads.h>
#include <unistd.h>

constexpr size_t MAX_ACTIVE_WORKERS = 8;

char *sanitize_filename(const char *raw, size_t len) {
  if (len > NAME_MAX) {
    len = NAME_MAX;
  }

  char *clean = malloc(len + 1);
  if (clean == nullptr) {
    return NULL;
  }

  size_t out = 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)raw[i];

    if (c < 32 || c == 127) {
      continue;
    }

    if (c == '/') {
      clean[out++] = '_';
    } else {
      clean[out++] = (char)c;
    }
  }
  clean[out] = '\0';

  if (out == 0 || strcmp(clean, ".") == 0 || strcmp(clean, "..") == 0) {
    free(clean);
    return strdup("unnamed_torrent");
  }

  return clean;
}

bool torrent_info_init(TorrentInfo *ti, const BencodeDict *dict) {
  StringView raw_name;

  if (bencode_dict_get_str(dict, "name", &raw_name) && raw_name.length > 0) {
    ti->name = sanitize_filename(raw_name.data, raw_name.length);
  } else {
    ti->name = strdup("unnamed_torrent");
  }

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
  StringView pieces;
  if (!bencode_dict_get_str(dict, "pieces", &pieces) ||
      pieces.length != expected_pieces_len) {
    fprintf(stderr, "Error: 'info.pieces' missing or invalid\n");
    return false;
  }

  ti->pieces = malloc(expected_pieces_len);
  if (ti->pieces == nullptr) {
    return false;
  }

  memcpy(ti->pieces, pieces.data, expected_pieces_len);
  return true;
}

void torrent_info_destroy(TorrentInfo *ti) {
  free(ti->pieces);
  free(ti->name);
}

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
  size_t file_size = (size_t)sb.st_size;

  char *data = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
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

  const BencodeDict *root;
  if (!bencode_get_dict(&bencode, &root)) {
    fprintf(stderr, "Error: Torrent root is not a dictionary\n");
    goto cleanup_bencode;
  }

  StringView announce;
  if (!bencode_dict_get_str(root, "announce", &announce)) {
    fprintf(stderr, "Error: Missing or invalid 'announce' string\n");
    goto cleanup_bencode;
  }

  t->tracker_url = strndup(announce.data, announce.length);
  if (t->tracker_url == nullptr) {
    goto cleanup_bencode;
  }

  Bencode info_raw;
  const BencodeDict *info_dict;
  if (!BencodeDict_get(root, sv_build("info"), &info_raw) ||
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

  memcpy(t->peer_id, "-EDUSKI-", 8);
  getrandom(t->peer_id + 8, 12, 0);
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
  munmap(data, file_size);

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

DEFINE_CONCURRENT_QUEUE(size_t, PieceQueue)
DEFINE_CONCURRENT_QUEUE(Peer, PeerQueue)

typedef struct {
  PieceQueue *piece_queue;
  PeerQueue *peer_queue;
  const Torrent *torrent;
  uint8_t *out;
  _Atomic size_t downloaded_pieces;
} WorkerContext;

int worker(void *vctx) {
  WorkerContext *ctx = vctx;
  size_t total_pieces = ctx->torrent->info->piece_count;

  Peer peer;
  while (ctx->downloaded_pieces < total_pieces &&
         PeerQueue_pop(ctx->peer_queue, &peer)) {
    PeerConnection conn;
    if (!peer_connect(&peer, ctx->torrent, &conn)) {
      fprintf(stderr, "[worker] Handshake failed with %s:%u\n", peer.ip,
              peer.port);
      continue;
    }

    size_t consecutive_missing = 0;
    size_t piece_index;
    while (PieceQueue_pop(ctx->piece_queue, &piece_index)) {
      size_t offset = piece_index * ctx->torrent->info->piece_length;
      PeerDownloadStatus status = peer_download_piece(
          &conn, ctx->torrent->info, piece_index, ctx->out + offset);

      if (status == PEER_DL_OK) {
        ctx->downloaded_pieces++;
        consecutive_missing = 0;
        continue;
      }

      PieceQueue_push(ctx->piece_queue, piece_index);

      switch (status) {
      case PEER_DL_CHOKED:
        fprintf(stderr, "[worker] Choked by %s:%u on piece %zu, waiting...\n",
                peer.ip, peer.port, piece_index);
        PeerQueue_push(ctx->peer_queue, peer);
        goto disconnect_peer;

      case PEER_DL_MISSING_PIECE:
        size_t remaining_pieces = total_pieces - ctx->downloaded_pieces;
        if (++consecutive_missing >= remaining_pieces) {
          fprintf(
              stderr,
              "[worker] Peer %s:%u missing all remaining pieces, dropping\n",
              peer.ip, peer.port);
          goto disconnect_peer;
        }
        break;

      default:
        fprintf(stderr,
                "[worker] Fatal error (%d) on piece %zu from %s:%u, dropping\n",
                status, piece_index, peer.ip, peer.port);
        goto disconnect_peer;
      }
    }

  disconnect_peer:
    peer_disconnect(&conn);
  }

  return (ctx->downloaded_pieces == total_pieces) ? EXIT_SUCCESS : EXIT_FAILURE;
}

bool torrent_download_to_file(Torrent *t, const char *out_path) {
  int fd = open(out_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    perror("open");
    return false;
  }

  if (ftruncate(fd, (off_t)t->info->length) != 0) {
    perror("ftruncate");
    close(fd);
    return false;
  }

  uint8_t *dest = mmap(NULL, (size_t)t->info->length, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, 0);
  close(fd);

  if (dest == MAP_FAILED) {
    perror("mmap");
    return false;
  }

  bool success = false;

  PeerVector peers;
  if (!PeerVector_init(&peers, 0)) {
    goto cleanup_mmap;
  }

  if (!tracker_get_peers(t, &peers) || peers.count == 0) {
    fprintf(stderr, "No peers returned from tracker\n");
    goto cleanup_peers;
  }

  PieceQueue piece_queue;
  if (!PieceQueue_init(&piece_queue, t->info->piece_count)) {
    goto cleanup_peers;
  }

  PeerQueue peer_queue;
  if (!PeerQueue_init(&peer_queue, peers.count)) {
    goto cleanup_piece_queue;
  }

  for (size_t i = 0; i < t->info->piece_count; i++) {
    PieceQueue_push(&piece_queue, i);
  }

  size_t peer_count = peers.count;
  for (size_t i = 0; i < peer_count; i++) {
    PeerQueue_push(&peer_queue, peers.data[i]);
  }
  PeerVector_destroy(&peers);

  size_t worker_count =
      peer_count > MAX_ACTIVE_WORKERS ? MAX_ACTIVE_WORKERS : peer_count;
  thrd_t workers[MAX_ACTIVE_WORKERS];

  WorkerContext ctx = {
      .piece_queue = &piece_queue,
      .peer_queue = &peer_queue,
      .torrent = t,
      .out = dest,
      .downloaded_pieces = 0,
  };

  for (size_t i = 0; i < worker_count; i++) {
    thrd_create(&workers[i], worker, (void *)&ctx);
  }

  for (size_t i = 0; i < worker_count; i++) {
    thrd_join(workers[i], nullptr);
  }

  success = (ctx.downloaded_pieces == t->info->piece_count);

  PeerQueue_destroy(&peer_queue);

cleanup_piece_queue:
  PieceQueue_destroy(&piece_queue);

cleanup_peers:
  if (peers.data != nullptr) {
    PeerVector_destroy(&peers);
  }

cleanup_mmap:
  if (success) {
    msync(dest, (size_t)t->info->length, MS_SYNC);
  }
  munmap(dest, (size_t)t->info->length);

  return success;
}
