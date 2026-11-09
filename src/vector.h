#pragma once
#include "common.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define VECTOR_TYPE(T, name)                                                   \
  typedef struct name {                                                        \
    T *data;                                                                   \
    size_t capacity;                                                           \
    size_t count;                                                              \
  } name;

#define VECTOR_METHODS(T, name)                                                \
  static inline bool name##_init(name *v, size_t capacity) {                   \
    v->capacity = capacity > 0 ? next_pow2(capacity) : 128;                    \
    v->count = 0;                                                              \
    v->data = malloc(v->capacity * sizeof(T));                                 \
    return v->data != nullptr;                                                 \
  }                                                                            \
                                                                               \
  static inline void name##_destroy(name *v) {                                 \
    free(v->data);                                                             \
    v->data = nullptr;                                                         \
    v->count = 0;                                                              \
    v->capacity = 0;                                                           \
  }                                                                            \
                                                                               \
  static inline bool name##_get(name *v, size_t index, T *out) {               \
    if (index >= v->count) {                                                   \
      return false;                                                            \
    }                                                                          \
                                                                               \
    *out = v->data[index];                                                     \
    return true;                                                               \
  }                                                                            \
                                                                               \
  static inline bool name##_push(name *v, T value) {                           \
    if (v->count == v->capacity) {                                             \
      size_t new_cap = v->capacity * 2;                                        \
      T *new_data = realloc(v->data, new_cap * sizeof(T));                     \
      if (new_data == nullptr) {                                               \
        return false;                                                          \
      }                                                                        \
                                                                               \
      v->data = new_data;                                                      \
      v->capacity = new_cap;                                                   \
    }                                                                          \
                                                                               \
    v->data[v->count++] = value;                                               \
    return true;                                                               \
  }                                                                            \
                                                                               \
  static inline bool name##_pop(name *v, T *out) {                             \
    if (v->count == 0) {                                                       \
      return false;                                                            \
    }                                                                          \
                                                                               \
    *out = v->data[--v->count];                                                \
    return true;                                                               \
  }                                                                            \
                                                                               \
  static inline bool name##_insert(name *v, size_t index, T value) {           \
    if (index >= v->count) {                                                   \
      return false;                                                            \
    }                                                                          \
                                                                               \
    v->data[index] = value;                                                    \
    return true;                                                               \
  }                                                                            \
                                                                               \
  static inline bool name##_remove(name *v, size_t index) {                    \
    if (index >= v->count) {                                                   \
      return false;                                                            \
    }                                                                          \
                                                                               \
    v->count--;                                                                \
    if (index < v->count) {                                                    \
      memmove(&v->data[index], &v->data[index + 1],                            \
              (v->count - index) * sizeof(T));                                 \
    }                                                                          \
    return true;                                                               \
  }                                                                            \
                                                                               \
  static inline T *name##_grow(name *v, size_t n) {                            \
    if (n == 0) {                                                              \
      return &v->data[v->count];                                               \
    }                                                                          \
                                                                               \
    size_t needed = v->count + n;                                              \
    if (needed > v->capacity) {                                                \
      size_t new_cap = next_pow2(needed);                                      \
      T *new_data = realloc(v->data, new_cap * sizeof(T));                     \
      if (new_data == nullptr) {                                               \
        return nullptr;                                                        \
      }                                                                        \
                                                                               \
      v->data = new_data;                                                      \
      v->capacity = new_cap;                                                   \
    }                                                                          \
                                                                               \
    T *dest = &v->data[v->count];                                              \
    v->count += n;                                                             \
    return dest;                                                               \
  }                                                                            \
                                                                               \
  static inline bool name##_extend(name *v, const T *items, size_t n) {        \
    if (n == 0) {                                                              \
      return true;                                                             \
    }                                                                          \
                                                                               \
    T *dest = name##_grow(v, n);                                               \
    if (dest == nullptr) {                                                     \
      return false;                                                            \
    }                                                                          \
                                                                               \
    memcpy(dest, items, n * sizeof(T));                                        \
    return true;                                                               \
  }

#define DEFINE_VECTOR(T, name)                                                 \
  VECTOR_TYPE(T, name)                                                         \
  VECTOR_METHODS(T, name)
