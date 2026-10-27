#pragma once

#include <cstddef>
#include <cstdint>

namespace bbs {

// SHA-256 e HMAC-SHA256 autonomi (nessuna dipendenza esterna): lib/bbs resta
// pura e verificabile sul PC. Servono alle news da MQTT (bbs_news.h): il
// payload MQTT viaggia in chiaro, quindi si autentica con un HMAC. Non e'
// codice a prestazioni critiche (poche decine di byte per messaggio).

constexpr size_t kSha256DigestLen = 32;

class Sha256 {
public:
  Sha256() { reset(); }

  void reset();
  void update(const uint8_t* data, size_t len);
  // Scrive 32 byte in 'out'. Dopo finish() l'oggetto va riportato con reset().
  void finish(uint8_t out[kSha256DigestLen]);

private:
  uint32_t _h[8];
  uint8_t _block[64];
  size_t _block_len;
  uint64_t _total_len;

  void compress(const uint8_t block[64]);
};

void sha256(const uint8_t* data, size_t len, uint8_t out[kSha256DigestLen]);

// HMAC-SHA256 (RFC 2104) con chiave di lunghezza qualsiasi.
void hmacSha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len,
                uint8_t out[kSha256DigestLen]);

// Confronto a tempo costante (non esce al primo byte diverso).
bool constantTimeEqual(const uint8_t* a, const uint8_t* b, size_t len);

} // namespace bbs
