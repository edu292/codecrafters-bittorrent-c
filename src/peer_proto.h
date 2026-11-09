#pragma once

#include "bencode_internal.h"
#include <corsair/torrent.h>
#include <corsair/types.h>
#include <stddef.h>
#include <stdint.h>

constexpr size_t BLOCK_SIZE = 16ULL * 1024;

typedef struct __attribute__((packed)) {
  uint8_t pstrlen;
  char pstr[19];
  uint8_t reserved[8];
  Sha1Hash info_hash;
  PeerId peer_id;
} Handshake;

typedef enum : uint8_t {
  EXTENDED_MESSAGES = 20,
} ProtocolExtension;

static inline void handshake_advertise_extension(Handshake *h,
                                                 ProtocolExtension pe) {
  h->reserved[7 - (pe / 8)] |= (uint8_t)(1U << (pe % 8));
}

static inline bool handshake_supports_extension(const Handshake *h,
                                                ProtocolExtension pe) {
  return (h->reserved[7 - (pe / 8)] & (uint8_t)(1U << (pe % 8))) != 0;
}

typedef enum : uint8_t {
  MSG_CHOKE = 0,
  MSG_UNCHOKE = 1,
  MSG_INTERESTED = 2,
  MSG_NOT_INTERESTED = 3,
  _MSG_ZERO_PAYLOAD_END = MSG_NOT_INTERESTED,

  MSG_HAVE = 4,
  MSG_BITFIELD = 5,
  MSG_REQUEST = 6,
  MSG_PIECE = 7,
  MSG_CANCEL = 8,
  MSG_EXTENDED = 20,
} MessageId;

typedef struct {
  MessageId id;
  size_t payload_len;
  void *payload;
} Message;

typedef struct {
  uint32_t index;
  uint32_t begin;
  uint32_t length;
} MsgRequestPayload;

typedef struct {
  uint32_t index;
  uint32_t begin;
  uint8_t block[];
} MsgPiecePayload;

typedef enum : uint8_t {
  METADATA_REQUEST = 0,
  METADATA_DATA = 1,
  METADATA_REJECT = 2,
} MetadataMsgType;

static inline bool msg_has_payload(MessageId id) {
  return id > _MSG_ZERO_PAYLOAD_END;
}

typedef enum : uint8_t {
  EXTENDED_MSG_HANDSHAKE = 0,
  EXTENDED_MSG_METADATA,
  _EXT_COUNT,
} ExtendedMessageId;

typedef struct {
  ExtendedMessageId extended_id;
  BencodeDict header;
  const uint8_t *extra;
  size_t extra_len;
  void *payload;
} ExtendedMessage;
