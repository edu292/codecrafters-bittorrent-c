#pragma once

#include "vector.h"
#include <corsair/peer.h>
#include <stdint.h>

VECTOR_METHODS(Peer, PeerVector)

typedef enum : uint8_t {
  PEER_AM_CHOKING = 1 << 0,
  PEER_AM_CHOKED = 1 << 1,
  PEER_AM_INTERESTED = 1 << 2,
  PEER_AM_WANTED = 1 << 3,
  PEER_EXTENSIONS_ENABLED = 1 << 4,
} PeerState;

constexpr PeerState PEER_STATE_INIT = PEER_AM_CHOKING | PEER_AM_CHOKED;

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

static inline void peer_state_set(PeerConnection *pc, PeerState flags) {
  pc->state |= flags;
}

static inline void peer_state_clear(PeerConnection *pc, PeerState flags) {
  pc->state &= ~flags;
}

static inline bool peer_state_is(const PeerConnection *pc, PeerState flags) {
  return (pc->state & flags) != 0;
}

static inline bool peer_state_is_all(const PeerConnection *pc,
                                     PeerState flags) {
  return (pc->state & flags) == flags;
}

typedef enum {
  PEER_WAIT_OK,
  PEER_WAIT_ERR_SOCKET,
  PEER_WAIT_ERR_CHOKED,
  PEER_WAIT_ERR_PROTO,
} PeerWaitResult;
