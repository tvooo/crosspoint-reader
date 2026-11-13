#pragma once
// One-shot SHA1()/SHA256() for the simulator's host-crypto shims; the browser has no OpenSSL.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace browser_sha {
inline uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

// Pads per FIPS 180-4 and feeds 64-byte blocks to `block`.
template <typename Block>
void process(const unsigned char* data, size_t len, Block block) {
  unsigned char buf[64];
  size_t i = 0;
  for (; i + 64 <= len; i += 64) block(data + i);
  size_t rest = len - i;
  std::memcpy(buf, data + i, rest);
  buf[rest++] = 0x80;
  if (rest > 56) {
    std::memset(buf + rest, 0, 64 - rest);
    block(buf);
    rest = 0;
  }
  std::memset(buf + rest, 0, 56 - rest);
  const uint64_t bits = static_cast<uint64_t>(len) * 8;
  for (int b = 0; b < 8; ++b) buf[63 - b] = static_cast<unsigned char>(bits >> (8 * b));
  block(buf);
}

inline uint32_t be32(const unsigned char* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline void put32(unsigned char* p, uint32_t v) {
  for (int b = 0; b < 4; ++b) p[b] = static_cast<unsigned char>(v >> (24 - 8 * b));
}
}  // namespace browser_sha

inline unsigned char* SHA1(const unsigned char* data, size_t len, unsigned char* out) {
  using namespace browser_sha;
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  process(data, len, [&](const unsigned char* p) {
    uint32_t w[80];
    for (int t = 0; t < 16; ++t) w[t] = be32(p + 4 * t);
    for (int t = 16; t < 80; ++t) w[t] = rotl(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int t = 0; t < 80; ++t) {
      uint32_t f, k;
      if (t < 20) f = (b & c) | (~b & d), k = 0x5A827999;
      else if (t < 40) f = b ^ c ^ d, k = 0x6ED9EBA1;
      else if (t < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
      else f = b ^ c ^ d, k = 0xCA62C1D6;
      const uint32_t tmp = rotl(a, 5) + f + e + k + w[t];
      e = d, d = c, c = rotl(b, 30), b = a, a = tmp;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
  });
  for (int i = 0; i < 5; ++i) put32(out + 4 * i, h[i]);
  return out;
}

inline unsigned char* SHA256(const unsigned char* data, size_t len, unsigned char* out) {
  using namespace browser_sha;
  static constexpr uint32_t K[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  process(data, len, [&](const unsigned char* p) {
    uint32_t w[64];
    for (int t = 0; t < 16; ++t) w[t] = be32(p + 4 * t);
    for (int t = 16; t < 64; ++t) {
      const uint32_t s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
      const uint32_t s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
      w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int t = 0; t < 64; ++t) {
      const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[t] + w[t];
      const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
  });
  for (int i = 0; i < 8; ++i) put32(out + 4 * i, h[i]);
  return out;
}
