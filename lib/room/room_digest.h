#pragma once

#include <cstddef>
#include <cstdint>

// Riepilogo giornaliero del room server: testo e conteggi (parte pura, nessuna
// dipendenza da Arduino o MeshCore). Il testo e' specifico del room server,
// diverso da bbs::formatDigest.

namespace room {

constexpr uint32_t kDigestWindowSecs = 24u * 3600u;
constexpr size_t kAuthorPrefixLen = 8;  // byte della chiave autore usati per distinguere gli utenti

struct DigestCounts {
  uint32_t posts;    // post nelle ultime 24 ore (compresi quelli di sistema)
  uint32_t authors;  // autori distinti, esclusi i post di sistema (chiave del nodo)
};

// 'author_prefixes': n * kAuthorPrefixLen byte, uno per post. Un post conta
// se timestamp != 0 e ha meno di 24 ore (un timestamp nel futuro conta).
// 'own_key' e' la chiave del nodo (confrontata sui primi kAuthorPrefixLen byte).
DigestCounts countRecent(const uint32_t* timestamps, const uint8_t* author_prefixes, size_t n,
                         const uint8_t* own_key, uint32_t now);

// Scrive in 'out' (al piu' cap-1 byte + terminatore) "Ultime 24h: 7 post da 3
// utenti". Ritorna la lunghezza, 0 (out vuoto) se non ci sono post.
size_t formatDigestText(const DigestCounts& c, char* out, size_t cap);

// Come sopra a partire dagli array; 0 se nessun post nelle ultime 24 ore.
size_t formatDigest(const uint32_t* timestamps, const uint8_t* author_prefixes, size_t n, const uint8_t* own_key,
                    uint32_t now, char* out, size_t cap);

constexpr const char* kDigestNoPosts = "Ultime 24h: nessun post";

// "HH:MM" -> ora e minuti. false se non valido.
bool parseHourMinute(const char* s, uint8_t& hour, uint8_t& minute);
// "+H", "-H:MM", "H" (segno opzionale), da -12:00 a +14:00. Ritorna minuti.
bool parseTzOffset(const char* s, int16_t& offset_min);

} // namespace room
