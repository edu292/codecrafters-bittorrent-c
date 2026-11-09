#pragma once
#include "common.h"
#include <stddef.h>
#include <stdlib.h>
#include <threads.h>

#define CONCURRENT_QUEUE_TYPE(T, name)                                         \
  typedef struct name {                                                        \
    T *data;                                                                   \
    size_t capacity;                                                           \
    size_t head;                                                               \
    size_t tail;                                                               \
    size_t count;                                                              \
    mtx_t lock;                                                                \
  } name;

#define CONCURRENT_QUEUE_METHODS(T, name)                                      \
  static inline bool name##_init(name *q, size_t initial_cap) {                \
    q->capacity = initial_cap > 16 ? next_pow2(initial_cap) : 16;              \
    q->head = 0;                                                               \
    q->tail = 0;                                                               \
    q->count = 0;                                                              \
    if (mtx_init(&q->lock, mtx_plain) != thrd_success) {                       \
      return false;                                                            \
    }                                                                          \
                                                                               \
    q->data = malloc(q->capacity * sizeof(T));                                 \
    if (q->data == nullptr) {                                                  \
      mtx_destroy(&q->lock);                                                   \
      return false;                                                            \
    }                                                                          \
    return true;                                                               \
  }                                                                            \
                                                                               \
  static inline void name##_destroy(name *q) {                                 \
    free(q->data);                                                             \
    q->data = nullptr;                                                         \
    q->capacity = 0;                                                           \
    q->head = 0;                                                               \
    q->tail = 0;                                                               \
    q->count = 0;                                                              \
    mtx_destroy(&q->lock);                                                     \
  }                                                                            \
                                                                               \
  static inline bool name##_push(name *q, T item) {                            \
    mtx_lock(&q->lock);                                                        \
    if (q->count == q->capacity) {                                             \
      mtx_unlock(&q->lock);                                                    \
      return false;                                                            \
    }                                                                          \
                                                                               \
    size_t mask = q->capacity - 1;                                             \
    q->data[q->tail] = item;                                                   \
    q->tail = (q->tail + 1) & mask;                                            \
    q->count++;                                                                \
    mtx_unlock(&q->lock);                                                      \
    return true;                                                               \
  }                                                                            \
                                                                               \
  static inline bool name##_pop(name *q, T *out) {                             \
    mtx_lock(&q->lock);                                                        \
    if (q->count == 0) {                                                       \
      mtx_unlock(&q->lock);                                                    \
      return false;                                                            \
    }                                                                          \
                                                                               \
    size_t mask = q->capacity - 1;                                             \
    *out = q->data[q->head];                                                   \
    q->head = (q->head + 1) & mask;                                            \
    q->count--;                                                                \
    mtx_unlock(&q->lock);                                                      \
    return true;                                                               \
  }

#define DEFINE_CONCURRENT_QUEUE(T, name)                                       \
  CONCURRENT_QUEUE_TYPE(T, name)                                               \
  CONCURRENT_QUEUE_METHODS(T, name)
