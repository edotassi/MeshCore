#include "bbs_sha256.h"

#include <cstring>

namespace bbs {

namespace {

const uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

} // namespace

void Sha256::reset() {
  static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  memcpy(_h, init, sizeof(_h));
  _block_len = 0;
  _total_len = 0;
}

void Sha256::compress(const uint8_t block[64]) {
  uint32_t w[64];
  for (size_t i = 0; i < 16; i++) {
    w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
           ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
  }
  for (size_t i = 16; i < 64; i++) {
    uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = _h[0], b = _h[1], c = _h[2], d = _h[3], e = _h[4], f = _h[5], g = _h[6], h = _h[7];
  for (size_t i = 0; i < 64; i++) {
    uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + s1 + ch + kK[i] + w[i];
    uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  _h[0] += a;
  _h[1] += b;
  _h[2] += c;
  _h[3] += d;
  _h[4] += e;
  _h[5] += f;
  _h[6] += g;
  _h[7] += h;
}

void Sha256::update(const uint8_t* data, size_t len) {
  _total_len += len;
  while (len > 0) {
    size_t n = 64 - _block_len;
    if (n > len) n = len;
    memcpy(_block + _block_len, data, n);
    _block_len += n;
    data += n;
    len -= n;
    if (_block_len == 64) {
      compress(_block);
      _block_len = 0;
    }
  }
}

void Sha256::finish(uint8_t out[kSha256DigestLen]) {
  uint64_t bit_len = _total_len * 8;
  uint8_t pad = 0x80;
  update(&pad, 1);
  pad = 0;
  while (_block_len != 56) update(&pad, 1);
  uint8_t len_be[8];
  for (size_t i = 0; i < 8; i++) len_be[i] = (uint8_t)(bit_len >> (56 - 8 * i));
  update(len_be, 8);

  for (size_t i = 0; i < 8; i++) {
    out[i * 4] = (uint8_t)(_h[i] >> 24);
    out[i * 4 + 1] = (uint8_t)(_h[i] >> 16);
    out[i * 4 + 2] = (uint8_t)(_h[i] >> 8);
    out[i * 4 + 3] = (uint8_t)(_h[i]);
  }
}

void sha256(const uint8_t* data, size_t len, uint8_t out[kSha256DigestLen]) {
  Sha256 s;
  s.update(data, len);
  s.finish(out);
}

void hmacSha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len,
                uint8_t out[kSha256DigestLen]) {
  uint8_t k[64];
  memset(k, 0, sizeof(k));
  if (key_len > sizeof(k)) {
    sha256(key, key_len, k);
  } else if (key_len > 0) {
    memcpy(k, key, key_len);
  }

  uint8_t pad[64];
  for (size_t i = 0; i < 64; i++) pad[i] = (uint8_t)(k[i] ^ 0x36);
  Sha256 inner;
  inner.update(pad, sizeof(pad));
  inner.update(msg, msg_len);
  uint8_t inner_hash[kSha256DigestLen];
  inner.finish(inner_hash);

  for (size_t i = 0; i < 64; i++) pad[i] = (uint8_t)(k[i] ^ 0x5c);
  Sha256 outer;
  outer.update(pad, sizeof(pad));
  outer.update(inner_hash, sizeof(inner_hash));
  outer.finish(out);
}

bool constantTimeEqual(const uint8_t* a, const uint8_t* b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

} // namespace bbs
