#include "room_digest.h"

#include <cstdio>
#include <cstring>

namespace room {

DigestCounts countRecent(const uint32_t* timestamps, const uint8_t* author_prefixes, size_t n,
                         const uint8_t* own_key, uint32_t now) {
  DigestCounts c = {0, 0};
  // Autori gia' visti: al massimo n, ma si confronta col prefisso nell'array
  // stesso (O(n^2), n e' al piu' qualche centinaio).
  for (size_t i = 0; i < n; i++) {
    if (timestamps[i] == 0) continue;
    uint32_t age = (now >= timestamps[i]) ? (now - timestamps[i]) : 0;
    if (age >= kDigestWindowSecs) continue;
    c.posts++;

    const uint8_t* a = author_prefixes + i * kAuthorPrefixLen;
    if (own_key && memcmp(a, own_key, kAuthorPrefixLen) == 0) continue;  // post di sistema

    bool seen = false;
    for (size_t j = 0; j < i && !seen; j++) {
      if (timestamps[j] == 0) continue;
      uint32_t age_j = (now >= timestamps[j]) ? (now - timestamps[j]) : 0;
      if (age_j >= kDigestWindowSecs) continue;
      seen = memcmp(a, author_prefixes + j * kAuthorPrefixLen, kAuthorPrefixLen) == 0;
    }
    if (!seen) c.authors++;
  }
  return c;
}

size_t formatDigestText(const DigestCounts& c, char* out, size_t cap) {
  if (cap == 0) return 0;
  out[0] = 0;
  if (c.posts == 0) return 0;
  int n;
  if (c.authors == 0) {
    n = snprintf(out, cap, "Ultime 24h: %lu post", (unsigned long)c.posts);
  } else {
    n = snprintf(out, cap, "Ultime 24h: %lu post da %lu %s", (unsigned long)c.posts, (unsigned long)c.authors,
                 c.authors == 1 ? "utente" : "utenti");
  }
  if (n < 0) {
    out[0] = 0;
    return 0;
  }
  return (size_t)n < cap ? (size_t)n : cap - 1;
}

size_t formatDigest(const uint32_t* timestamps, const uint8_t* author_prefixes, size_t n, const uint8_t* own_key,
                    uint32_t now, char* out, size_t cap) {
  return formatDigestText(countRecent(timestamps, author_prefixes, n, own_key, now), out, cap);
}

bool parseHourMinute(const char* s, uint8_t& hour, uint8_t& minute) {
  const char* p = s;
  unsigned h = 0;
  size_t nd = 0;
  while (*p >= '0' && *p <= '9' && nd < 3) {
    h = h * 10 + (unsigned)(*p - '0');
    p++;
    nd++;
  }
  if (nd == 0 || nd > 2 || *p != ':') return false;
  p++;
  if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9' || p[2] != 0) return false;
  unsigned m = (unsigned)(p[0] - '0') * 10 + (unsigned)(p[1] - '0');
  if (h > 23 || m > 59) return false;
  hour = (uint8_t)h;
  minute = (uint8_t)m;
  return true;
}

bool parseTzOffset(const char* s, int16_t& offset_min) {
  const char* p = s;
  int sign = 1;
  if (*p == '+' || *p == '-') {
    if (*p == '-') sign = -1;
    p++;
  }
  unsigned h = 0;
  size_t nd = 0;
  while (*p >= '0' && *p <= '9' && nd < 3) {
    h = h * 10 + (unsigned)(*p - '0');
    p++;
    nd++;
  }
  if (nd == 0 || nd > 2) return false;
  unsigned m = 0;
  if (*p == ':') {
    p++;
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return false;
    m = (unsigned)(p[0] - '0') * 10 + (unsigned)(p[1] - '0');
    p += 2;
  }
  if (*p != 0 || m > 59) return false;
  int total = sign * (int)(h * 60 + m);
  if (total < -720 || total > 840) return false;
  offset_min = (int16_t)total;
  return true;
}

} // namespace room
