#pragma once
#include "common.h"
#include <stddef.h>
#include <stdint.h>

typedef enum : uint8_t {
  SLOT_EMPTY = 0,
  SLOT_OCCUPIED,
  SLOT_DELETED,
} SlotStatus;

#define HASHMAP_TYPE(K, V, name)                                               \
  typedef struct {                                                             \
    K key;                                                                     \
    V value;                                                                   \
    SlotStatus status;                                                         \
  } name##_entry;                                                              \
                                                                               \
  typedef struct name {                                                        \
    name##_entry *entries;                                                     \
    size_t capacity;                                                           \
    size_t count;                                                              \
    size_t tombstones;                                                         \
  } name;

#define HASHMAP_METHODS(K, V, name, hash_fn, eq_fn)                            \
  static inline bool name##_init(name *m, size_t initial_cap) {                \
    m->capacity = initial_cap > 16 ? next_pow2(initial_cap) : 16;              \
    m->count = 0;                                                              \
    m->tombstones = 0;                                                         \
    m->entries = calloc(m->capacity, sizeof(name##_entry));                    \
    return m->entries != nullptr;                                              \
  }                                                                            \
                                                                               \
  static inline void name##_destroy(name *m) {                                 \
    free(m->entries);                                                          \
    m->entries = nullptr;                                                      \
    m->capacity = 0;                                                           \
    m->count = 0;                                                              \
    m->tombstones = 0;                                                         \
  }                                                                            \
                                                                               \
  static inline bool name##_resize(name *m) {                                  \
    size_t new_cap =                                                           \
        m->count * 2 > m->capacity ? m->capacity * 2 : m->capacity;            \
    name##_entry *new_entries = calloc(new_cap, sizeof(name##_entry));         \
    if (new_entries == nullptr) {                                              \
      return false;                                                            \
    }                                                                          \
                                                                               \
    name##_entry *old_entries = m->entries;                                    \
    size_t old_cap = m->capacity;                                              \
    size_t mask = new_cap - 1;                                                 \
    for (size_t i = 0; i < old_cap; i++) {                                     \
      if (old_entries[i].status != SLOT_OCCUPIED) {                            \
        continue;                                                              \
      }                                                                        \
                                                                               \
      size_t idx = hash_fn(old_entries[i].key) & mask;                         \
      while (new_entries[idx].status != SLOT_EMPTY) {                          \
        idx = (idx + 1) & mask;                                                \
      }                                                                        \
                                                                               \
      new_entries[idx].key = old_entries[i].key;                               \
      new_entries[idx].value = old_entries[i].value;                           \
      new_entries[idx].status = SLOT_OCCUPIED;                                 \
    }                                                                          \
                                                                               \
    free(old_entries);                                                         \
    m->entries = new_entries;                                                  \
    m->capacity = new_cap;                                                     \
    m->count = 0;                                                              \
    m->tombstones = 0;                                                         \
    return true;                                                               \
  }                                                                            \
                                                                               \
  static inline bool name##_set(name *m, K key, V value) {                     \
    if ((m->count + m->tombstones + 1) * 10 >= m->capacity * 7) {              \
      if (!name##_resize(m)) {                                                 \
        return false;                                                          \
      }                                                                        \
    }                                                                          \
                                                                               \
    size_t mask = m->capacity - 1;                                             \
    size_t idx = hash_fn(key) & mask;                                          \
    size_t first_deleted = (size_t)-1;                                         \
                                                                               \
    while (m->entries[idx].status != SLOT_EMPTY) {                             \
      if (m->entries[idx].status == SLOT_OCCUPIED) {                           \
        if (eq_fn(m->entries[idx].key, key)) {                                 \
          m->entries[idx].value = value;                                       \
          return true;                                                         \
        }                                                                      \
      } else if (first_deleted == (size_t)-1) {                                \
        first_deleted = idx;                                                   \
      }                                                                        \
      idx = (idx + 1) & mask;                                                  \
    }                                                                          \
                                                                               \
    size_t insert_idx = (first_deleted != (size_t)-1) ? first_deleted : idx;   \
    if (m->entries[insert_idx].status == SLOT_DELETED) {                       \
      m->tombstones--;                                                         \
    }                                                                          \
    m->entries[insert_idx].key = key;                                          \
    m->entries[insert_idx].value = value;                                      \
    m->entries[insert_idx].status = SLOT_OCCUPIED;                             \
    m->count++;                                                                \
    return true;                                                               \
  }                                                                            \
                                                                               \
  static inline bool name##_get(const name *m, K key, V *out) {                \
    if (m->count == 0) {                                                       \
      return false;                                                            \
    }                                                                          \
                                                                               \
    size_t mask = m->capacity - 1;                                             \
    size_t idx = hash_fn(key) & mask;                                          \
                                                                               \
    while (m->entries[idx].status != SLOT_EMPTY) {                             \
      if (m->entries[idx].status == SLOT_OCCUPIED &&                           \
          eq_fn(m->entries[idx].key, key)) {                                   \
        *out = m->entries[idx].value;                                          \
        return true;                                                           \
      }                                                                        \
                                                                               \
      idx = (idx + 1) & mask;                                                  \
    }                                                                          \
                                                                               \
    return false;                                                              \
  }                                                                            \
                                                                               \
  static inline bool name##_remove(name *m, K key) {                           \
    if (m->count == 0) {                                                       \
      return false;                                                            \
    }                                                                          \
                                                                               \
    size_t mask = m->capacity - 1;                                             \
    size_t idx = hash_fn(key) & mask;                                          \
                                                                               \
    while (m->entries[idx].status != SLOT_EMPTY) {                             \
      if (m->entries[idx].status == SLOT_OCCUPIED &&                           \
          eq_fn(m->entries[idx].key, key)) {                                   \
        m->entries[idx].status = SLOT_DELETED;                                 \
        m->count--;                                                            \
        m->tombstones++;                                                       \
        return true;                                                           \
      }                                                                        \
                                                                               \
      idx = (idx + 1) & mask;                                                  \
    }                                                                          \
                                                                               \
    return false;                                                              \
  }                                                                            \
                                                                               \
  static inline const name##_entry **name##_get_entries(const name *m) {       \
    if (m->count == 0) {                                                       \
      return nullptr;                                                          \
    }                                                                          \
                                                                               \
    const name##_entry **out =                                                 \
        malloc(m->count * sizeof(const name##_entry *));                       \
    if (out == nullptr) {                                                      \
      return nullptr;                                                          \
    }                                                                          \
                                                                               \
    size_t k = 0;                                                              \
    for (size_t i = 0; i < m->capacity; i++) {                                 \
      if (m->entries[i].status == SLOT_OCCUPIED) {                             \
        out[k++] = &m->entries[i];                                             \
      }                                                                        \
    }                                                                          \
                                                                               \
    return out;                                                                \
  }

#define DEFINE_HASHMAP(K, V, name, hash_fn, eq_fn)                             \
  HASHMAP_TYPE(K, V, name)                                                     \
  HASHMAP_METHODS(K, V, name, hash_fn, eq_fn)
