#ifndef UTIL_H
#define UTIL_H

#include <byteswap.h>
#include <stdint.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t ntohll(uint64_t x) { return bswap_64(x); }
static inline uint64_t htonll(uint64_t x) { return bswap_64(x); }
#elif BYTE_ORDER == __BIG__ENDIAN
static inline uint64_t ntohll(uint64_t x) { return x; }
static inline uint64_t htonll(uint64_t x) { return x; }
#endif

#endif