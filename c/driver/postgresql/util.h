// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#pragma once

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif
#include <cstring>
#include <sstream>
#include <string>
#include <utility>

#if defined(__linux__)
#include <endian.h>
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#endif

#include "adbc.h"

namespace adbcpq {

#define CONCAT(x, y) x##y
#define MAKE_NAME(x, y) CONCAT(x, y)

#if defined(_WIN32) && defined(_MSC_VER)
static inline uint32_t SwapNetworkToHost(uint32_t x) { return ntohl(x); }
static inline uint32_t SwapHostToNetwork(uint32_t x) { return htonl(x); }
static inline uint64_t SwapNetworkToHost(uint64_t x) { return ntohll(x); }
static inline uint64_t SwapHostToNetwork(uint64_t x) { return htonll(x); }
#elif defined(_WIN32)
// e.g., msys2, where ntohll is not necessarily defined
static inline uint32_t SwapNetworkToHost(uint32_t x) { return ntohl(x); }
static inline uint32_t SwapHostToNetwork(uint32_t x) { return htonl(x); }
static inline uint64_t SwapNetworkToHost(uint64_t x) {
  return (((x & 0xFFULL) << 56) | ((x & 0xFF00ULL) << 40) | ((x & 0xFF0000ULL) << 24) |
          ((x & 0xFF000000ULL) << 8) | ((x & 0xFF00000000ULL) >> 8) |
          ((x & 0xFF0000000000ULL) >> 24) | ((x & 0xFF000000000000ULL) >> 40) |
          ((x & 0xFF00000000000000ULL) >> 56));
}
static inline uint64_t SwapHostToNetwork(uint64_t x) { return SwapNetworkToHost(x); }
#elif defined(__APPLE__)
static inline uint32_t SwapNetworkToHost(uint32_t x) { return OSSwapBigToHostInt32(x); }
static inline uint32_t SwapHostToNetwork(uint32_t x) { return OSSwapHostToBigInt32(x); }
static inline uint64_t SwapNetworkToHost(uint64_t x) { return OSSwapBigToHostInt64(x); }
static inline uint64_t SwapHostToNetwork(uint64_t x) { return OSSwapHostToBigInt64(x); }
#else
static inline uint32_t SwapNetworkToHost(uint32_t x) { return be32toh(x); }
static inline uint32_t SwapHostToNetwork(uint32_t x) { return htobe32(x); }
static inline uint64_t SwapNetworkToHost(uint64_t x) { return be64toh(x); }
static inline uint64_t SwapHostToNetwork(uint64_t x) { return htobe64(x); }
#endif

// see arrow/util/string_builder.h

template <typename Head>
static inline void StringBuilderRecursive(std::stringstream& stream, Head&& head) {
  stream << head;
}

template <typename Head, typename... Tail>
static inline void StringBuilderRecursive(std::stringstream& stream, Head&& head,
                                          Tail&&... tail) {
  StringBuilderRecursive(stream, std::forward<Head>(head));
  StringBuilderRecursive(stream, std::forward<Tail>(tail)...);
}

template <typename... Args>
static inline std::string StringBuilder(Args&&... args) {
  std::stringstream ss;
  StringBuilderRecursive(ss, std::forward<Args>(args)...);
  return ss.str();
}

static inline void ReleaseError(struct AdbcError* error) {
  delete[] error->message;
  error->message = nullptr;
  error->release = nullptr;
}

template <typename... Args>
static inline void SetError(struct AdbcError* error, Args&&... args) {
  if (!error) return;
  std::string message = StringBuilder("[libpq] ", std::forward<Args>(args)...);
  if (error->message) {
    message.reserve(message.size() + 1 + std::strlen(error->message));
    message.append(1, '\n');
    message.append(error->message);
    delete[] error->message;
  }
  error->message = new char[message.size() + 1];
  message.copy(error->message, message.size());
  error->message[message.size()] = '\0';
  error->release = ReleaseError;
}

#define CHECK_IMPL(NAME, EXPR)          \
  do {                                  \
    const AdbcStatusCode NAME = (EXPR); \
    if (NAME != ADBC_STATUS_OK) {       \
      return NAME;                      \
    }                                   \
  } while (false)
#define CHECK(EXPR) CHECK_IMPL(MAKE_NAME(adbc_status_, __COUNTER__), EXPR)

#define CHECK_NA_ADBC_IMPL(NAME, EXPR, ERROR)                    \
  do {                                                           \
    const int NAME = (EXPR);                                     \
    if (NAME) {                                                  \
      SetError((ERROR), #EXPR " failed: ", std::strerror(NAME)); \
      return ADBC_STATUS_INTERNAL;                               \
    }                                                            \
  } while (false)
/// Check an errno-style code and return an ADBC code if necessary.
#define CHECK_NA_ADBC(EXPR, ERROR) \
  CHECK_NA_ADBC_IMPL(MAKE_NAME(errno_status_, __COUNTER__), EXPR, ERROR)

#define CHECK_NA_IMPL(NAME, EXPR) \
  do {                            \
    const int NAME = (EXPR);      \
    if (NAME) return NAME;        \
  } while (false)

/// Check an errno-style code and return it if necessary.
#define CHECK_NA(EXPR) CHECK_NA_IMPL(MAKE_NAME(errno_status_, __COUNTER__), EXPR)

/// Endianness helpers

static inline uint16_t LoadNetworkUInt16(const char* buf) {
  uint16_t v = 0;
  std::memcpy(&v, buf, sizeof(uint16_t));
  return ntohs(v);
}

static inline uint32_t LoadNetworkUInt32(const char* buf) {
  uint32_t v = 0;
  std::memcpy(&v, buf, sizeof(uint32_t));
  return ntohl(v);
}

static inline int64_t LoadNetworkUInt64(const char* buf) {
  uint64_t v = 0;
  std::memcpy(&v, buf, sizeof(uint64_t));
  return SwapNetworkToHost(v);
}

static inline int16_t LoadNetworkInt16(const char* buf) {
  return static_cast<int16_t>(LoadNetworkUInt16(buf));
}

static inline int32_t LoadNetworkInt32(const char* buf) {
  return static_cast<int32_t>(LoadNetworkUInt32(buf));
}

static inline int64_t LoadNetworkInt64(const char* buf) {
  return static_cast<int64_t>(LoadNetworkUInt64(buf));
}

static inline double LoadNetworkFloat8(const char* buf) {
  uint64_t vint;
  memcpy(&vint, buf, sizeof(uint64_t));
  vint = SwapHostToNetwork(vint);
  double out;
  memcpy(&out, &vint, sizeof(double));
  return out;
}

static inline uint32_t ToNetworkInt32(int32_t v) {
  return SwapHostToNetwork(static_cast<uint32_t>(v));
}

static inline uint64_t ToNetworkInt64(int64_t v) {
  return SwapHostToNetwork(static_cast<uint64_t>(v));
}

static inline uint64_t ToNetworkFloat8(double v) {
  uint64_t vint;
  memcpy(&vint, &v, sizeof(uint64_t));
  return SwapHostToNetwork(vint);
}

}  // namespace adbcpq
