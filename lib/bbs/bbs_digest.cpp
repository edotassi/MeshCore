#include "bbs_digest.h"

#include <cstdio>
#include <cstring>

#include "bbs_binary_io.h"
#include "bbs_strings_it.h"

namespace bbs {

namespace {

constexpr uint8_t kDigestFileVersion = 1;
// version, enabled, hour, minute, tz(2), eu_dst, psk_len, psk[32], last_sent_day(4)
constexpr size_t kDigestFileSize = 1 + 1 + 1 + 1 + 2 + 1 + 1 + kDigestPskMaxLen + 4;

constexpr int16_t kTzMinMin = -720;
constexpr int16_t kTzMaxMin = 840;

// Algoritmi di data civile (Howard Hinnant), validi per l'epoch Unix.
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= (m <= 2);
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

// 0 = domenica (il 1970-01-01 era un giovedi').
unsigned weekdayFromDays(int64_t days) {
  int64_t w = (days + 4) % 7;
  if (w < 0) w += 7;
  return (unsigned)w;
}

unsigned daysInMonth(unsigned month) {
  // Usato solo per marzo e ottobre (31 giorni).
  return (month == 3 || month == 10) ? 31 : 30;
}

int64_t lastSundayDays(int year, unsigned month) {
  int64_t last = daysFromCivil(year, month, daysInMonth(month));
  return last - weekdayFromDays(last);
}

bool isContinuationByte(char c) { return ((uint8_t)c & 0xC0) == 0x80; }

// Il piu' grande n <= max tale che il taglio a n byte non spezzi un code point.
size_t utf8SafeLen(const char* s, size_t len, size_t max) {
  if (len <= max) return len;
  size_t n = max;
  while (n > 0 && isContinuationByte(s[n])) n--;
  return n;
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

int b64Val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

} // namespace

size_t base64Decode(const char* in, uint8_t* out, size_t out_cap) {
  size_t len = strlen(in);
  size_t pad = 0;
  while (len > 0 && in[len - 1] == '=') {
    len--;
    pad++;
  }
  if (pad > 2) return 0;
  if (pad > 0 && ((len + pad) % 4) != 0) return 0;
  if (len == 0 || (len % 4) == 1) return 0;

  size_t out_len = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < len; i++) {
    int v = b64Val(in[i]);
    if (v < 0) return 0;
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (out_len >= out_cap) return 0;
      out[out_len++] = (uint8_t)((acc >> bits) & 0xFF);
    }
  }
  return out_len;
}

size_t decodeKey(const char* in, uint8_t* out, size_t out_cap) {
  size_t len = strlen(in);
  if (len == 32 || len == 64) {
    bool all_hex = true;
    for (size_t i = 0; i < len && all_hex; i++) all_hex = hexVal(in[i]) >= 0;
    if (all_hex) {
      if (len / 2 > out_cap) return 0;
      for (size_t i = 0; i < len / 2; i++) out[i] = (uint8_t)(hexVal(in[2 * i]) * 16 + hexVal(in[2 * i + 1]));
      return len / 2;
    }
  }
  return base64Decode(in, out, out_cap);
}

DigestSettings::DigestSettings()
    : enabled(false), hour(8), minute(0), tz_offset_min(60), eu_dst(true), psk{}, psk_len(0), last_sent_day(0) {
  setPublicPsk();
}

void DigestSettings::setPublicPsk() {
  memset(psk, 0, sizeof(psk));
  psk_len = (uint8_t)base64Decode(kDigestPublicPskBase64, psk, sizeof(psk));
}

bool DigestSettings::usesPublicPsk() const {
  uint8_t pub[kDigestPskMaxLen];
  size_t n = base64Decode(kDigestPublicPskBase64, pub, sizeof(pub));
  return n == psk_len && memcmp(pub, psk, n) == 0;
}

bool isEuDst(uint32_t utc_ts) {
  int64_t days = (int64_t)(utc_ts / 86400u);
  int year;
  unsigned m, d;
  civilFromDays(days, year, m, d);
  int64_t start = lastSundayDays(year, 3) * 86400 + 3600;
  int64_t end = lastSundayDays(year, 10) * 86400 + 3600;
  return (int64_t)utc_ts >= start && (int64_t)utc_ts < end;
}

int64_t localTime(uint32_t utc_ts, const DigestSettings& s) {
  int64_t t = (int64_t)utc_ts + (int64_t)s.tz_offset_min * 60;
  if (s.eu_dst && isEuDst(utc_ts)) t += 3600;
  return t;
}

uint32_t localDayNumber(int64_t local_ts) {
  if (local_ts < 0) return 0;
  return (uint32_t)(local_ts / 86400);
}

uint32_t localMinutesOfDay(int64_t local_ts) {
  if (local_ts < 0) return 0;
  return (uint32_t)((local_ts % 86400) / 60);
}

void civilFromDays(int64_t z, int& year, unsigned& month, unsigned& day) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = (int64_t)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  day = doy - (153 * mp + 2) / 5 + 1;
  month = mp < 10 ? mp + 3 : mp - 9;
  year = (int)(y + (month <= 2));
}

DigestDecision decide(const DigestSettings& s, uint32_t now_utc) {
  if (!s.enabled) return DigestDecision::WAIT;
  if (now_utc < kDigestMinValidEpoch) return DigestDecision::WAIT;

  int64_t local = localTime(now_utc, s);
  if (localDayNumber(local) == s.last_sent_day) return DigestDecision::WAIT;

  uint32_t scheduled = (uint32_t)s.hour * 60u + s.minute;
  uint32_t now_min = localMinutesOfDay(local);
  if (now_min < scheduled) return DigestDecision::WAIT;
  if (now_min - scheduled < kDigestCatchUpWindowMin) return DigestDecision::SEND;
  return DigestDecision::SKIP_DAY;
}

// --- DigestStore ---

DigestStore::DigestStore(IFileSystem& fs, const char* path) : _fs(fs), _path(path) { load(); }

void DigestStore::load() {
  _settings = DigestSettings();

  IFile* f = _fs.open(_path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return;
  }
  uint8_t buf[kDigestFileSize];
  size_t n = f->read(buf, sizeof(buf));
  f->close();
  if (n != sizeof(buf) || buf[0] != kDigestFileVersion) return;

  DigestSettings s;
  s.enabled = buf[1] != 0;
  s.hour = buf[2];
  s.minute = buf[3];
  s.tz_offset_min = (int16_t)getU16(&buf[4]);
  s.eu_dst = buf[6] != 0;
  s.psk_len = buf[7];
  memcpy(s.psk, &buf[8], kDigestPskMaxLen);
  s.last_sent_day = getU32(&buf[8 + kDigestPskMaxLen]);

  if (s.hour > 23 || s.minute > 59) return;
  if (s.tz_offset_min < kTzMinMin || s.tz_offset_min > kTzMaxMin) return;
  if (s.psk_len != 16 && s.psk_len != 32) return;
  _settings = s;
}

bool DigestStore::save(const DigestSettings& s) {
  uint8_t buf[kDigestFileSize];
  buf[0] = kDigestFileVersion;
  buf[1] = s.enabled ? 1 : 0;
  buf[2] = s.hour;
  buf[3] = s.minute;
  putU16(&buf[4], (uint16_t)s.tz_offset_min);
  buf[6] = s.eu_dst ? 1 : 0;
  buf[7] = s.psk_len;
  memcpy(&buf[8], s.psk, kDigestPskMaxLen);
  putU32(&buf[8 + kDigestPskMaxLen], s.last_sent_day);

  char tmp_path[40];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", _path);
  IFile* dst = _fs.open(tmp_path, 'w');
  if (!dst || !dst->valid()) {
    if (dst) dst->close();
    return false;
  }
  bool ok = (dst->write(buf, sizeof(buf)) == sizeof(buf));
  dst->close();
  if (!ok) {
    _fs.remove(tmp_path);
    return false;
  }
  _fs.remove(_path);
  if (!_fs.rename(tmp_path, _path)) return false;

  _settings = s;  // persisti-poi-applica
  return true;
}

// --- Formattazione ---

size_t formatDigest(PostStore& posts, RoomRegistry& rooms, uint32_t now_utc, char* out, size_t cap) {
  if (cap == 0) return 0;
  out[0] = 0;
  if (cap == 1) return 0;
  const size_t budget = cap - 1;

  uint32_t after_ts = (now_utc > 86400u) ? (now_utc - 86400u) : 0;

  // Solo conteggi per stanza: nessun testo ne' autore dei post, che qui
  // finirebbero su un canale di gruppo aperto.
  char list[BBS_MAX_ROOMS * (BBS_ROOM_NAME_LEN + 14) + 1];
  size_t list_len = 0;
  uint32_t total = 0;

  for (size_t i = 0; i < rooms.count(); i++) {
    const RoomInfo& room = rooms.at(i);
    uint32_t count = posts.countUnread(room.id, after_ts);
    if (count == 0) continue;
    total += count;
    int n = snprintf(list + list_len, sizeof(list) - list_len, "%s%s %u", list_len ? ", " : "", room.name,
                     (unsigned)count);
    if (n > 0 && list_len + (size_t)n < sizeof(list)) list_len += (size_t)n;
  }
  if (total == 0) return 0;

  char head[32];
  size_t head_len = (size_t)snprintf(head, sizeof(head), strings::kDigestHeadFmt, (unsigned)total);

  size_t len = 0;
  auto put = [&](const char* s, size_t n) {
    memcpy(out + len, s, n);
    len += n;
  };

  const char* ell = "...";
  const size_t ell_len = 3;

  if (head_len + list_len <= budget) {
    put(head, head_len);
    put(list, list_len);
  } else if (head_len + ell_len <= budget) {
    size_t keep = utf8SafeLen(list, list_len, budget - head_len - ell_len);
    put(head, head_len);
    put(list, keep);
    put(ell, ell_len);
  } else {
    size_t keep = utf8SafeLen(head, head_len, budget);
    put(head, keep);
  }
  out[len] = 0;
  return len;
}

} // namespace bbs
