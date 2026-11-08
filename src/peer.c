#include "peer.h"
#include "torrent.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <openssl/sha.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

constexpr size_t PEER_BLOCK_SIZE = 16ULL * 1024;

typedef struct __attribute__((packed)) {
  uint8_t pstrlen;
  char pstr[19];
  uint8_t reserved[8];
  Sha1Hash info_hash;
  uint8_t peer_id[20];
} PeerHandshake;

typedef enum : uint8_t {
  PEER_MSG_CHOKE = 0,
  PEER_MSG_UNCHOKE = 1,
  PEER_MSG_INTERESTED = 2,
  PEER_MSG_NOT_INTERESTED = 3,
  PEER_MSG_HAVE = 4,
  PEER_MSG_BITFIELD = 5,
  PEER_MSG_REQUEST = 6,
  PEER_MSG_PIECE = 7,
  PEER_MSG_CANCEL = 8,
} PeerMessageId;

typedef struct {
  uint32_t length;
  PeerMessageId id;
  void *payload;
} PeerMessage;

typedef struct {
  uint32_t index;
  uint32_t begin;
  uint32_t length;
} PeerMsgRequestPayload;

typedef struct {
  uint32_t index;
  uint32_t begin;
  uint8_t block[];
} PeerMsgPiecePayload;

bool writev_all(int fd, struct iovec *iov, int iovcnt) {
  while (iovcnt > 0) {
    ssize_t n = writev(fd, iov, iovcnt);
    if (n <= 0) {
      return false;
    }

    size_t written = (size_t)n;
    while (iovcnt > 0 && written >= iov->iov_len) {
      written -= iov->iov_len;
      iov++;
      iovcnt--;
    }

    if (iovcnt > 0 && written > 0) {
      iov->iov_base = (char *)iov->iov_base + written;
      iov->iov_len -= written;
    }
  }

  return true;
}

bool read_all(int fd, void *buf, size_t len) {
  uint8_t *ptr = (uint8_t *)buf;
  size_t remaining = len;

  while (remaining > 0) {
    ssize_t n = read(fd, ptr, remaining);
    if (n <= 0) {
      return false;
    }

    ptr += n;
    remaining -= (size_t)n;
  }

  return true;
}

bool write_all(int fd, const void *buf, size_t len) {
  const uint8_t *ptr = (const uint8_t *)buf;
  size_t remaining = len;

  while (remaining > 0) {
    ssize_t n = write(fd, ptr, remaining);
    if (n <= 0) {
      return false;
    }

    ptr += n;
    remaining -= (size_t)n;
  }

  return true;
}

void peer_from_bytes(Peer *p, const uint8_t *src) {
  snprintf(p->ip, sizeof(p->ip), "%u.%u.%u.%u", src[0], src[1], src[2], src[3]);

  p->port = (uint16_t)((src[4] << 8) | src[5]);
}

bool peer_from_str(Peer *p, const char *src) {
  const char *colon = strchr(src, ':');
  if (colon == nullptr) {
    return false;
  }

  size_t ip_len = (size_t)(colon - src);
  if (ip_len >= sizeof(p->ip)) {
    return false;
  }

  memcpy(p->ip, src, ip_len);
  p->ip[ip_len] = '\0';

  p->port = (uint16_t)strtoul(colon + 1, nullptr, 10);
  return true;
}

static inline bool peer_has_piece(const PeerConnection *pc,
                                  size_t piece_index) {
  size_t byte_index = piece_index / 8;
  if (byte_index >= pc->bitfield_length) {
    return false;
  }

  uint8_t bit_mask = (uint8_t)(1U << (7 - (piece_index % 8)));
  return (pc->bitfield[byte_index] & bit_mask) != 0;
}

static inline void peer_set_piece(PeerConnection *pc, size_t piece_index) {
  size_t byte_idx = piece_index / 8;
  if (byte_idx >= pc->bitfield_length) {
    return;
  }

  pc->bitfield[byte_idx] |= (uint8_t)(1U << (7 - (piece_index % 8)));
}

bool peer_recv_message(int fd, PeerMessage *msg, size_t *out_payload_len) {
  while (true) {
    if (!read_all(fd, &msg->length, sizeof(msg->length))) {
      return false;
    }

    msg->length = ntohl(msg->length);
    if (msg->length == 0) {
      continue;
    }

    if (!read_all(fd, &msg->id, sizeof(msg->id))) {
      return false;
    }

    size_t payload_len = msg->length - sizeof(msg->id);
    *out_payload_len = payload_len;

    msg->payload = nullptr;
    if (payload_len > 0) {
      msg->payload = malloc(payload_len);
      if (msg->payload == nullptr) {
        return false;
      }

      if (!read_all(fd, msg->payload, payload_len)) {
        free(msg->payload);
        return false;
      }
    }

    return true;
  }
}

bool peer_wait_for_msg(PeerConnection *pc, PeerMessageId expected_id,
                       PeerMessage *out, size_t *out_payload_len) {
  size_t payload_len;
  while (peer_recv_message(pc->fd, out, &payload_len)) {
    switch (out->id) {
    case PEER_MSG_CHOKE:
      pc->is_choked = true;
      if (expected_id == PEER_MSG_PIECE) {
        free(out->payload);
        out->payload = nullptr;
        return false;
      }
      break;

    case PEER_MSG_UNCHOKE:
      pc->is_choked = false;
      break;

    case PEER_MSG_HAVE:
      if (payload_len == sizeof(uint32_t)) {
        peer_set_piece(pc, ntohl(*(uint32_t *)out->payload));
      }
      break;

    case PEER_MSG_BITFIELD:
      if (pc->bitfield_length == 0) {
        pc->bitfield = malloc(payload_len);
        if (pc->bitfield != nullptr) {
          memcpy(pc->bitfield, out->payload, payload_len);
          pc->bitfield_length = payload_len;
        }
      } else {
        if (payload_len != pc->bitfield_length) {
          return false;
        }

        memcpy(pc->bitfield, out->payload, payload_len);
      }
      break;
    }

    if (out->id == expected_id) {
      if (out_payload_len != nullptr) {
        *out_payload_len = payload_len;
      }
      return true;
    }

    free(out->payload);
    out->payload = nullptr;
  }

  return false;
}

bool peer_send_msg(int fd, PeerMessageId id, void *payload,
                   size_t payload_len) {
  uint32_t net_len = htonl((uint32_t)(payload_len + sizeof(id)));
  struct iovec iov[3] = {
      {.iov_base = &net_len, .iov_len = sizeof(net_len)},
      {.iov_base = &id, .iov_len = sizeof(id)},
      {.iov_base = payload, .iov_len = payload_len},
  };

  int iovcnt = payload_len > 0 ? 3 : 2;
  return writev_all(fd, iov, iovcnt);
}

static inline PeerMsgRequestPayload
peer_msg_request_build(uint32_t index, uint32_t begin, uint32_t length) {
  return (PeerMsgRequestPayload){
      .index = htonl(index),
      .begin = htonl(begin),
      .length = htonl(length),
  };
}

static inline PeerMsgPiecePayload *
peer_msg_piece_parse(PeerMessage *msg, size_t payload_len, size_t *block_size) {
  if (payload_len < sizeof(PeerMsgPiecePayload)) {
    return nullptr;
  }

  PeerMsgPiecePayload *p = msg->payload;
  p->index = ntohl(p->index);
  p->begin = ntohl(p->begin);

  *block_size = payload_len - sizeof(PeerMsgPiecePayload);
  return p;
}

bool peer_connect(const Peer *p, const Torrent *t, PeerConnectFlags pcf,
                  PeerConnection *pc) {
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    return false;
  }

  struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(p->port)};
  if (inet_pton(AF_INET, p->ip, &addr.sin_addr) <= 0) {
    fprintf(stderr, "Invalid or unsupported IP: %s\n", p->ip);
    close(fd);
    return false;
  }

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("connect");
    close(fd);
    return false;
  }

  PeerHandshake hndshk = {};
  hndshk.pstrlen = 19;
  memcpy(hndshk.pstr, "BitTorrent protocol", 19);
  memcpy(hndshk.info_hash.bytes, t->info_hash.bytes, 20);
  memcpy(hndshk.peer_id, t->peer_id, 20);

  if (pcf & PEER_OPT_EXTENSIONS) {
    hndshk.reserved[5] |= 1 << 4;
  }

  if (!write_all(fd, &hndshk, sizeof(hndshk))) {
    fprintf(stderr, "Failed to send handshake\n");
    close(fd);
    return false;
  }

  PeerHandshake res;
  if (!read_all(fd, &res, sizeof(res))) {
    fprintf(stderr, "Failed to read handshake response\n");
    close(fd);
    return false;
  }

  if (res.pstrlen != 19 || memcmp(res.pstr, "BitTorrent protocol", 19) != 0 ||
      memcmp(res.info_hash.bytes, t->info_hash.bytes, 20) != 0) {
    fprintf(stderr,
            "Handshake validation failed: protocol/info_hash mismatch\n");
    close(fd);
    return false;
  }

  if (t->info != nullptr) {
    size_t bf_len = (t->info->piece_count + 7) / 8;
    uint8_t *bf = calloc(bf_len, sizeof(uint8_t));
    if (bf == nullptr) {
      close(fd);
      return false;
    }

    pc->bitfield = bf;
    pc->bitfield_length = bf_len;
  } else {
    pc->bitfield = nullptr;
    pc->bitfield_length = 0;
  }

  pc->fd = fd;
  pc->is_choked = true;
  pc->is_interested = false;
  memcpy(pc->peer_id, res.peer_id, sizeof(pc->peer_id));

  return true;
}

void peer_disconnect(PeerConnection *pc) {
  free(pc->bitfield);
  pc->bitfield = nullptr;
  pc->bitfield_length = 0;
  close(pc->fd);
}

PeerDownloadStatus peer_download_piece(PeerConnection *pc,
                                       const TorrentInfo *ti,
                                       size_t piece_index, uint8_t *out) {
  size_t piece_length = torrent_info_piece_length(ti, piece_index);
  if (piece_length == 0) {
    return PEER_DL_INVALID_ARGS;
  }

  if (!pc->is_interested) {
    if (!peer_send_msg(pc->fd, PEER_MSG_INTERESTED, nullptr, 0)) {
      return PEER_DL_ERR_SOCKET;
    }

    pc->is_interested = true;
  }

  PeerMessage msg = {};
  if (pc->is_choked) {
    if (!peer_wait_for_msg(pc, PEER_MSG_UNCHOKE, &msg, nullptr)) {
      return PEER_DL_ERR_SOCKET;
    }

    free(msg.payload);
  }

  if (!peer_has_piece(pc, piece_index)) {
    return PEER_DL_MISSING_PIECE;
  }

  size_t block_count = (piece_length + PEER_BLOCK_SIZE - 1) / PEER_BLOCK_SIZE;
  for (size_t i = 0; i < block_count; i++) {
    uint32_t begin = (uint32_t)(i * PEER_BLOCK_SIZE);
    size_t remaining = piece_length - begin;
    uint32_t block_length =
        (uint32_t)(remaining > PEER_BLOCK_SIZE ? PEER_BLOCK_SIZE : remaining);
    PeerMsgRequestPayload req =
        peer_msg_request_build((uint32_t)piece_index, begin, block_length);

    if (!peer_send_msg(pc->fd, PEER_MSG_REQUEST, &req, sizeof(req))) {
      return PEER_DL_ERR_SOCKET;
    }
  }

  for (size_t i = 0; i < block_count; i++) {
    size_t payload_len;
    if (!peer_wait_for_msg(pc, PEER_MSG_PIECE, &msg, &payload_len)) {
      return pc->is_choked ? PEER_DL_CHOKED : PEER_DL_ERR_SOCKET;
    }

    size_t block_size;
    PeerMsgPiecePayload *payload =
        peer_msg_piece_parse(&msg, payload_len, &block_size);
    if (payload == nullptr || payload->index != piece_index ||
        payload->begin + block_size > piece_length) {
      free(msg.payload);
      return PEER_DL_CORRUPTED;
    }

    memcpy(out + payload->begin, payload->block, block_size);
    free(msg.payload);
  }

  Sha1Hash piece_hash;
  SHA1(out, piece_length, piece_hash.bytes);
  if (memcmp(piece_hash.bytes, ti->pieces[piece_index].bytes,
             sizeof(Sha1Hash)) != 0) {
    return PEER_DL_CORRUPTED;
  }

  return PEER_DL_OK;
}

bool peer_download_piece_to_file(PeerConnection *pc, const TorrentInfo *ti,
                                 size_t piece_index, const char *out_path) {
  size_t piece_len = torrent_info_piece_length(ti, piece_index);
  if (piece_len == 0) {
    fprintf(stderr, "Error: Invalid piece index %zu\n", piece_index);
    return false;
  }

  int fd = open(out_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    perror("open");
    return false;
  }

  if (ftruncate(fd, (off_t)piece_len) != 0) {
    perror("ftruncate");
    close(fd);
    return false;
  }

  uint8_t *dest =
      mmap(NULL, piece_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);

  if (dest == MAP_FAILED) {
    perror("mmap");
    return false;
  }

  bool ok = (peer_download_piece(pc, ti, piece_index, dest) == PEER_DL_OK);
  if (!ok) {
    fprintf(stderr, "Error: Failed to download piece %zu\n", piece_index);
  }

  munmap(dest, piece_len);
  return ok;
}
