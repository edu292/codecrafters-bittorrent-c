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
#include <unistd.h>

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

bool peer_session_init(PeerSession *ps, const Peer *p,
                       const Sha1Hash *info_hash) {
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

  PeerHandshake req = {0};
  req.pstrlen = 19;
  memcpy(req.pstr, "BitTorrent protocol", 19);
  memcpy(req.info_hash.bytes, info_hash->bytes, 20);
  memcpy(req.peer_id, "00112233445566778899", 20);

  if (!write_all(fd, &req, sizeof(req))) {
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
      memcmp(res.info_hash.bytes, info_hash->bytes, 20) != 0) {
    fprintf(stderr,
            "Handshake validation failed: protocol/info_hash mismatch\n");
    close(fd);
    return false;
  }

  ps->fd = fd;
  memcpy(ps->peer_id, res.peer_id, sizeof(ps->peer_id));
  return true;
}

void peer_session_destroy(PeerSession *ps) { close(ps->fd); }

bool peer_recv_message(int fd, PeerMessage *msg) {
  while (true) {
    if (!read_all(fd, &msg->length, sizeof(msg->length))) {
      return false;
    }

    msg->length = ntohl(msg->length);
    if (!read_all(fd, &msg->id, sizeof(msg->id))) {
      return false;
    }

    msg->payload = nullptr;
    size_t payload_len = msg->length - 1;
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

bool peer_wait_for_msg(int fd, PeerMessageId expected_id, PeerMessage *out) {
  while (peer_recv_message(fd, out)) {
    if (out->id == expected_id) {
      return true;
    }

    free(out->payload);
  }

  return false;
}

bool peer_send_msg(int fd, PeerMessage *msg) {
  uint32_t net_len = htonl(msg->length);
  if (!write_all(fd, &net_len, sizeof(net_len))) {
    return false;
  }

  if (!write_all(fd, &msg->id, sizeof(msg->id))) {
    return false;
  }

  size_t payload_len = msg->length - sizeof(msg->id);
  return !(payload_len > 0 && !write_all(fd, msg->payload, payload_len));
}

bool peer_session_download_piece(PeerSession *ps, const TorrentInfo *ti,
                                 size_t index, uint8_t *out) {
  size_t piece_length = torrent_info_piece_length(ti, index);
  if (piece_length == 0) {
    return false;
  }

  PeerMessage msg;
  if (!peer_wait_for_msg(ps->fd, PEER_MSG_BITFIELD, &msg)) {
    return false;
  }

  free(msg.payload);

  msg.id = PEER_MSG_INTERESTED;
  msg.length = 1;
  peer_send_msg(ps->fd, &msg);

  if (!peer_wait_for_msg(ps->fd, PEER_MSG_UNCHOKE, &msg)) {
    return false;
  }

  size_t block_count = (piece_length + PEER_BLOCK_SIZE - 1) / PEER_BLOCK_SIZE;
  uint32_t piece_index_nl = htonl((uint32_t)index);
  for (size_t i = 0; i < block_count; i++) {
    uint32_t begin = (uint32_t)(i * PEER_BLOCK_SIZE);
    uint32_t block_length =
        (uint32_t)(piece_length - begin < PEER_BLOCK_SIZE ? piece_length - begin
                                                          : PEER_BLOCK_SIZE);
    PeerMsgRequestPayload payload = {
        .index = piece_index_nl,
        .begin = htonl(begin),
        .length = htonl(block_length),
    };

    msg.length = sizeof(PeerMsgRequestPayload) + 1;
    msg.id = PEER_MSG_REQUEST;
    msg.payload = &payload;

    if (!peer_send_msg(ps->fd, &msg)) {
      return false;
    }
  }

  for (size_t i = 0; i < block_count; i++) {
    if (!peer_wait_for_msg(ps->fd, PEER_MSG_PIECE, &msg)) {
      return false;
    }

    const size_t header_len = sizeof(msg.id) + sizeof(PeerMsgPiecePayload);
    const PeerMsgPiecePayload *payload = msg.payload;
    size_t block_size = msg.length - header_len;
    uint32_t begin = ntohl(payload->begin);
    memcpy(out + begin, payload->block, block_size);
    free(msg.payload);
  }

  Sha1Hash piece_hash;
  SHA1(out, piece_length, piece_hash.bytes);
  return memcmp(piece_hash.bytes, ti->pieces[index].bytes, sizeof(Sha1Hash)) ==
         0;
}

bool torrent_download_piece_to_file(PeerSession *ps, const TorrentInfo *ti,
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

  bool ok = peer_session_download_piece(ps, ti, piece_index, dest);
  if (!ok) {
    fprintf(stderr, "Error: Failed to download piece %zu\n", piece_index);
  }

  munmap(dest, piece_len);
  return ok;
}
