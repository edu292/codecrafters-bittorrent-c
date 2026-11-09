#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/uio.h>
#include <unistd.h>

static inline bool writev_all(int fd, struct iovec *iov, int iovcnt) {
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

static inline bool read_all(int fd, void *buf, size_t len) {
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

static inline bool write_all(int fd, const void *buf, size_t len) {
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
