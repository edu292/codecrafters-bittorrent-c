#include "peer.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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
  memcpy(p->ip, src, ip_len);
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
