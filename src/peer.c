#include "bencode_internal.h"
#include "bencode_write.h"
#include "io_util.h"
#include "peer_internal.h"
#include "peer_proto.h"
#include <arpa/inet.h>
#include <corsair/bencode.h>
#include <corsair/peer.h>
#include <corsair/torrent.h>
#include <errno.h>
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

bool peer_vector_init(PeerVector *pv, size_t capacity) {
  return PeerVector_init(pv, capacity);
}

void peer_vector_destroy(PeerVector *pv) { PeerVector_destroy(pv); }

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

bool peer_recv_message(int fd, Message *msg) {
  while (true) {
    uint32_t wire_len;
    if (!read_all(fd, &wire_len, sizeof(wire_len))) {
      return false;
    }

    wire_len = ntohl(wire_len);
    if (wire_len == 0) {
      continue;
    }

    if (!read_all(fd, &msg->id, sizeof(msg->id))) {
      return false;
    }

    size_t payload_len = wire_len - sizeof(msg->id);
    msg->payload_len = payload_len;

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

static bool peer_handle_msg(PeerConnection *pc, const Message *msg) {
  if (!msg_has_payload(msg->id) && msg->payload_len != 0) {
    return false;
  }

  switch (msg->id) {
  case MSG_CHOKE:
    peer_state_set(pc, PEER_AM_CHOKED);
    break;
  case MSG_UNCHOKE:
    peer_state_clear(pc, PEER_AM_CHOKED);
    break;
  case MSG_INTERESTED:
    peer_state_set(pc, PEER_AM_WANTED);
    break;
  case MSG_NOT_INTERESTED:
    peer_state_clear(pc, PEER_AM_WANTED);
    break;
  case MSG_HAVE:
    if (msg->payload_len != sizeof(uint32_t)) {
      return false;
    }

    peer_set_piece(pc, ntohl(*(const uint32_t *)msg->payload));
    break;
  case MSG_BITFIELD:
    return false;
  default:
    break;
  }

  return true;
}

PeerWaitResult peer_wait_for_msg(PeerConnection *pc, MessageId expected_id,
                                 Message *out) {
  while (peer_recv_message(pc->fd, out)) {
    if (!peer_handle_msg(pc, out)) {
      free(out->payload);
      out->payload = nullptr;
      return PEER_WAIT_ERR_PROTO;
    }

    if (out->id == MSG_CHOKE && expected_id == MSG_PIECE) {
      free(out->payload);
      out->payload = nullptr;
      return PEER_WAIT_ERR_CHOKED;
    }

    if (out->id == expected_id) {
      return PEER_WAIT_OK;
    }

    free(out->payload);
    out->payload = nullptr;
  }

  return PEER_WAIT_ERR_SOCKET;
}

static bool peer_recv_bitfield(PeerConnection *pc) {
  struct timeval tv = {.tv_sec = 3, .tv_usec = 0};
  if (setsockopt(pc->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    return false;
  }

  errno = 0;
  Message msg = {};
  bool ok = true;
  if (peer_recv_message(pc->fd, &msg)) {
    if (msg.id == MSG_BITFIELD) {
      if (pc->bitfield != nullptr) {
        if (msg.payload_len != pc->bitfield_length) {
          ok = false;
        } else {
          memcpy(pc->bitfield, msg.payload, msg.payload_len);
        }
      } else {
        pc->bitfield = malloc(msg.payload_len);
        if (pc->bitfield == nullptr) {
          ok = false;
        } else {
          memcpy(pc->bitfield, msg.payload, msg.payload_len);
          pc->bitfield_length = msg.payload_len;
        }
      }
    } else if (!peer_handle_msg(pc, &msg)) {
      ok = false;
    }

    free(msg.payload);
  } else {
    if (errno != 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      ok = false;
    }
  }

  struct timeval reset_tv = {.tv_sec = 0, .tv_usec = 0};
  setsockopt(pc->fd, SOL_SOCKET, SO_RCVTIMEO, &reset_tv, sizeof(reset_tv));

  return ok;
}

bool peer_send_msg(int fd, MessageId id, const void *payload,
                   size_t payload_len) {
  uint32_t net_len = htonl((uint32_t)(payload_len + sizeof(id)));
  struct iovec iov[3] = {
      {.iov_base = &net_len, .iov_len = sizeof(net_len)},
      {.iov_base = &id, .iov_len = sizeof(id)},
      {.iov_base = (void *)payload, .iov_len = payload_len},
  };

  int iovcnt = payload_len > 0 ? 3 : 2;
  return writev_all(fd, iov, iovcnt);
}

bool peer_send_extended_msg(PeerConnection *pc, ExtendedMessageId ext_id,
                            const void *payload, size_t payload_len) {
  if (!peer_state_is(pc, PEER_EXTENSIONS_ENABLED)) {
    return false;
  }

  uint32_t total_len = htonl((uint32_t)(1 + 1 + payload_len));
  uint8_t msg_id = MSG_EXTENDED;

  uint8_t peer_ext_id = 0;
  if (ext_id != EXT_MSG_HANDSHAKE) {
    peer_ext_id = pc->extensions_table[ext_id];
    if (peer_ext_id == 0) {
      return false;
    }
  }

  struct iovec iov[] = {
      {.iov_base = &total_len, .iov_len = sizeof(uint32_t)},
      {.iov_base = &msg_id, .iov_len = sizeof(MessageId)},
      {.iov_base = &peer_ext_id, .iov_len = sizeof(ExtendedMessageId)},
      {.iov_base = (void *)payload, .iov_len = payload_len},
  };

  return writev_all(pc->fd, iov, payload_len > 0 ? 4 : 3);
}

static inline MsgRequestPayload
peer_msg_request_build(uint32_t index, uint32_t begin, uint32_t length) {
  return (MsgRequestPayload){
      .index = htonl(index),
      .begin = htonl(begin),
      .length = htonl(length),
  };
}

static inline MsgPiecePayload *peer_msg_piece_parse(Message *msg,
                                                    size_t *block_size) {
  if (msg->payload_len < sizeof(MsgPiecePayload)) {
    return nullptr;
  }

  MsgPiecePayload *p = msg->payload;
  p->index = ntohl(p->index);
  p->begin = ntohl(p->begin);

  *block_size = msg->payload_len - sizeof(MsgPiecePayload);
  return p;
}

bool extended_message_parse(const Message *msg, ExtendedMessage *out) {
  if (msg->payload_len < 1) {
    return false;
  }

  const uint8_t *raw = msg->payload;
  out->extended_id = raw[0];

  Bencode bencode;
  const char *bencode_start = (const char *)(raw + 1);
  const char *bencode_end = parse_bencode(bencode_start, &bencode);
  if (bencode_end == nullptr) {
    return false;
  }

  if (bencode.type != BENCODE_DICT) {
    bencode_destroy(&bencode);
    return false;
  }

  out->header = *bencode.value.dict;
  free(bencode.value.dict);

  size_t parsed_len = (size_t)(bencode_end - (const char *)raw);
  out->extra = raw + parsed_len;
  out->extra_len = msg->payload_len - parsed_len;
  return true;
}

void extended_message_destroy(ExtendedMessage *msg) {
  BencodeDict_destroy(&msg->header);
}

bool peer_extended_messages_handshake(PeerConnection *pc) {
  CharVector handshake_payload;
  CharVector_init(&handshake_payload, 1024);
  bencode_write_dict_begin(&handshake_payload);
  bencode_write_str(&handshake_payload, "m");
  bencode_write_dict_begin(&handshake_payload);
  bencode_write_key_int(&handshake_payload, "ut_metadata", EXT_MSG_METADATA);
  bencode_write_dict_end(&handshake_payload);
  bencode_write_dict_end(&handshake_payload);

  if (!peer_send_extended_msg(pc, EXT_MSG_HANDSHAKE, handshake_payload.data,
                              handshake_payload.count)) {
    return false;
  }

  Message msg = {};
  peer_recv_message(pc->fd, &msg);
  if (msg.id != MSG_EXTENDED) {
    return false;
  }

  ExtendedMessage res = {};
  if (!extended_message_parse(&msg, &res)) {
    return false;
  };

  const BencodeDict *m;
  if (!bencode_dict_get_dict(&res.header, "m", &m)) {
    return false;
  }

  const BencodeDict_entry **entries = BencodeDict_get_entries(m);
  for (size_t i = 0; i < m->count; i++) {
    const BencodeDict_entry *entry = entries[i];
    if (sv_eq_cstr(entry->key, "ut_metadata")) {
      int64_t peer_extension_id;
      if (!bencode_get_int(&entry->value, &peer_extension_id)) {
        continue;
      }

      pc->extensions_table[EXT_MSG_METADATA] = (uint8_t)peer_extension_id;
    }
  }

  free(entries);
  return true;
}

bool peer_connect(const Peer *p, const Torrent *t, PeerConnection *pc) {
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

  static const char pstr[] = "BitTorrent protocol";
  size_t pstrlen = sizeof(pstr) - 1;
  Handshake hndshk = {
      .pstrlen = (uint8_t)pstrlen,
      .info_hash = t->info_hash,
      .peer_id = t->peer_id,
  };
  memcpy(hndshk.pstr, pstr, pstrlen);
  handshake_advertise_extension(&hndshk, EXTENDED_MESSAGES);

  if (!write_all(fd, &hndshk, sizeof(hndshk))) {
    close(fd);
    return false;
  }

  if (!read_all(fd, &hndshk, sizeof(hndshk))) {
    close(fd);
    return false;
  }

  if (hndshk.pstrlen != pstrlen || memcmp(hndshk.pstr, pstr, pstrlen) != 0 ||
      memcmp(hndshk.info_hash.bytes, t->info_hash.bytes, 20) != 0) {
    close(fd);
    return false;
  }

  if (t->info != nullptr) {
    size_t bf_len = (t->info->piece_count + 7) / 8;
    pc->bitfield = calloc(bf_len, sizeof(uint8_t));
    if (pc->bitfield == nullptr) {
      close(fd);
      return false;
    }
    pc->bitfield_length = bf_len;
  } else {
    pc->bitfield = nullptr;
    pc->bitfield_length = 0;
  }

  pc->fd = fd;
  pc->state = PEER_STATE_INIT;
  pc->peer_id = hndshk.peer_id;

  bool supports_extended_messages = false;
  if (handshake_supports_extension(&hndshk, EXTENDED_MESSAGES)) {
    peer_state_set(pc, PEER_EXTENSIONS_ENABLED);
    pc->extensions_table = calloc(_EXT_COUNT, sizeof(uint8_t));
    pc->extensions_table[EXT_MSG_HANDSHAKE] = EXT_MSG_HANDSHAKE;
    supports_extended_messages = true;
  } else {
    pc->extensions_table = nullptr;
  }

  if (!peer_recv_bitfield(pc)) {
    peer_disconnect(pc);
    return false;
  }

  if (supports_extended_messages && !peer_extended_messages_handshake(pc)) {
    peer_disconnect(pc);
    return false;
  }

  return true;
}

void peer_disconnect(PeerConnection *pc) {
  if (pc->bitfield != nullptr) {
    free(pc->bitfield);
    pc->bitfield = nullptr;
    pc->bitfield_length = 0;
  }

  if (peer_state_is(pc, PEER_EXTENSIONS_ENABLED)) {
    free(pc->extensions_table);
  }

  close(pc->fd);
}

bool peer_request_metadata(PeerConnection *pc) {
  CharVector payload;
  if (!CharVector_init(&payload, 1024)) {
    return false;
  }

  if (!bencode_write_dict_begin(&payload) ||
      !bencode_write_key_int(&payload, "msg_type", 0) ||
      !bencode_write_key_int(&payload, "piece", 0) ||
      !bencode_write_dict_end(&payload)) {
    CharVector_destroy(&payload);
    return false;
  }

  return peer_send_extended_msg(pc, EXT_MSG_METADATA, payload.data,
                                payload.count);
}

static inline PeerDownloadResult peer_dl_result_from_wait(PeerWaitResult res) {
  switch (res) {
  case PEER_WAIT_OK:
    return PEER_DL_OK;
  case PEER_WAIT_ERR_CHOKED:
    return PEER_DL_ERR_CHOKED;
  case PEER_WAIT_ERR_SOCKET:
    return PEER_DL_ERR_SOCKET;
  case PEER_WAIT_ERR_PROTO:
  default:
    return PEER_DL_ERR_CORRUPTED;
  }
}

PeerDownloadResult peer_download_piece(PeerConnection *pc,
                                       const TorrentInfo *ti,
                                       size_t piece_index, uint8_t *out) {
  size_t piece_length = torrent_info_piece_length(ti, piece_index);
  if (piece_length == 0) {
    return PEER_DL_ERR_INVALID_ARGS;
  }

  if (!peer_state_is(pc, PEER_AM_INTERESTED)) {
    if (!peer_send_msg(pc->fd, MSG_INTERESTED, nullptr, 0)) {
      return PEER_DL_ERR_SOCKET;
    }
    peer_state_set(pc, PEER_AM_INTERESTED);
  }

  Message msg = {};
  if (peer_state_is(pc, PEER_AM_CHOKED)) {
    PeerWaitResult res = peer_wait_for_msg(pc, MSG_UNCHOKE, &msg);
    free(msg.payload);
    if (res != PEER_WAIT_OK) {
      return peer_dl_result_from_wait(res);
    }
  }

  if (!peer_has_piece(pc, piece_index)) {
    return PEER_DL_ERR_MISSING_PIECE;
  }

  size_t block_count = (piece_length + BLOCK_SIZE - 1) / BLOCK_SIZE;
  for (size_t i = 0; i < block_count; i++) {
    uint32_t begin = (uint32_t)(i * BLOCK_SIZE);
    size_t remaining = piece_length - begin;
    uint32_t block_length =
        (uint32_t)(remaining > BLOCK_SIZE ? BLOCK_SIZE : remaining);
    MsgRequestPayload req =
        peer_msg_request_build((uint32_t)piece_index, begin, block_length);
    if (!peer_send_msg(pc->fd, MSG_REQUEST, &req, sizeof(req))) {
      return PEER_DL_ERR_SOCKET;
    }
  }

  for (size_t i = 0; i < block_count; i++) {
    PeerWaitResult res = peer_wait_for_msg(pc, MSG_PIECE, &msg);
    if (res != PEER_WAIT_OK) {
      return peer_dl_result_from_wait(res);
    }
    size_t block_size;
    MsgPiecePayload *payload = peer_msg_piece_parse(&msg, &block_size);
    if (payload == nullptr || payload->index != piece_index ||
        payload->begin + block_size > piece_length) {
      free(msg.payload);
      return PEER_DL_ERR_CORRUPTED;
    }

    memcpy(out + payload->begin, payload->block, block_size);
    free(msg.payload);
  }

  Sha1Hash piece_hash;
  SHA1(out, piece_length, piece_hash.bytes);
  if (memcmp(piece_hash.bytes, ti->pieces[piece_index].bytes,
             sizeof(Sha1Hash)) != 0) {
    return PEER_DL_ERR_CORRUPTED;
  }

  return PEER_DL_OK;
}

bool peer_download_piece_to_file(PeerConnection *pc, const TorrentInfo *ti,
                                 size_t piece_index, const char *out_path) {
  size_t piece_len = torrent_info_piece_length(ti, piece_index);
  if (piece_len == 0) {
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
