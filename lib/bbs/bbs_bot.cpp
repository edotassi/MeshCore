#include "bbs_bot.h"

#include <cstdio>
#include <cstring>

#include "bbs_binary_io.h"
#include "bbs_digest.h"
#include "bbs_strings_it.h"

namespace bbs {

namespace {

constexpr uint8_t kBotFileVersion = 1;
// Per canale: name[24], psk_len, psk[32].
constexpr size_t kBotChannelRecSize = kBotNameLen + 1 + kBotPskMaxLen;
// version, enabled, count, canali[4]
constexpr size_t kBotFileSize = 1 + 1 + 1 + kBotMaxChannels * kBotChannelRecSize;

bool isContinuationByte(char c) { return ((uint8_t)c & 0xC0) == 0x80; }

// Il piu' grande n <= max tale che il taglio a n byte non spezzi un code point.
size_t utf8SafeLen(const char* s, size_t len, size_t max) {
  if (len <= max) return len;
  size_t n = max;
  while (n > 0 && isContinuationByte(s[n])) n--;
  return n;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

bool equalsLower(const char* a, size_t a_len, const char* b) {
  size_t i = 0;
  for (; i < a_len; i++) {
    if (b[i] == 0 || lower(a[i]) != b[i]) return false;
  }
  return b[i] == 0;
}

bool equalsNoCase(const char* a, const char* b) {
  for (; *a != 0 && *b != 0; a++, b++) {
    if (lower(*a) != lower(*b)) return false;
  }
  return *a == 0 && *b == 0;
}

} // namespace

BotChannel::BotChannel() : name{}, psk{}, psk_len(0) {}

BotSettings::BotSettings() : enabled(false), channels{}, count(2) {
  strcpy(channels[0].name, "Public");
  channels[0].psk_len = (uint8_t)base64Decode(kDigestPublicPskBase64, channels[0].psk, sizeof(channels[0].psk));
  strcpy(channels[1].name, "#test");
  channels[1].psk_len = 0;
}

int BotSettings::find(const char* name) const {
  for (size_t i = 0; i < count; i++) {
    if (equalsNoCase(channels[i].name, name)) return (int)i;
  }
  return -1;
}

bool normalizeHashtagName(const char* in, char* out, size_t out_cap) {
  size_t len = strlen(in);
  if (in[0] != '#' || len < 2 || len > kBotNameMaxChars || out_cap < len + 1) return false;
  out[0] = '#';
  for (size_t i = 1; i < len; i++) {
    char c = lower(in[i]);
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    out[i] = c;
  }
  out[len] = 0;
  return true;
}

bool isValidKeyedName(const char* name) {
  size_t len = strlen(name);
  if (len == 0 || len > kBotNameMaxChars || name[0] == '#') return false;
  for (size_t i = 0; i < len; i++) {
    if ((uint8_t)name[i] <= ' ' || (uint8_t)name[i] == 0x7F) return false;
  }
  return true;
}

// --- BotStore ---

BotStore::BotStore(IFileSystem& fs, const char* path) : _fs(fs), _path(path) { load(); }

void BotStore::load() {
  _settings = BotSettings();

  IFile* f = _fs.open(_path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return;
  }
  uint8_t buf[kBotFileSize];
  size_t n = f->read(buf, sizeof(buf));
  f->close();
  if (n != sizeof(buf) || buf[0] != kBotFileVersion) return;

  BotSettings s;
  s.enabled = buf[1] != 0;
  s.count = buf[2];
  if (s.count > kBotMaxChannels) return;
  for (size_t i = 0; i < kBotMaxChannels; i++) {
    const uint8_t* rec = &buf[3 + i * kBotChannelRecSize];
    BotChannel& c = s.channels[i];
    memset(&c, 0, sizeof(c));
    memcpy(c.name, rec, kBotNameLen);
    c.psk_len = rec[kBotNameLen];
    memcpy(c.psk, &rec[kBotNameLen + 1], kBotPskMaxLen);
    if (i >= s.count) continue;

    if (c.name[kBotNameLen - 1] != 0) return;
    if (c.psk_len == 0) {
      char norm[kBotNameLen];
      if (!normalizeHashtagName(c.name, norm, sizeof(norm)) || strcmp(norm, c.name) != 0) return;
    } else {
      if (c.psk_len != 16 && c.psk_len != 32) return;
      if (!isValidKeyedName(c.name)) return;
    }
  }
  _settings = s;
}

bool BotStore::save(const BotSettings& s) {
  if (s.count > kBotMaxChannels) return false;

  uint8_t buf[kBotFileSize];
  memset(buf, 0, sizeof(buf));
  buf[0] = kBotFileVersion;
  buf[1] = s.enabled ? 1 : 0;
  buf[2] = s.count;
  for (size_t i = 0; i < s.count; i++) {
    uint8_t* rec = &buf[3 + i * kBotChannelRecSize];
    memcpy(rec, s.channels[i].name, kBotNameLen);
    rec[kBotNameLen] = s.channels[i].psk_len;
    memcpy(&rec[kBotNameLen + 1], s.channels[i].psk, kBotPskMaxLen);
  }

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
  _generation++;
  return true;
}

// --- Riconoscimento e formattazione ---

bool parseTestTrigger(const char* text, char* sender_out, size_t cap) {
  if (cap == 0) return false;
  const char* sep = strstr(text, ": ");
  if (sep == nullptr || sep == text) return false;

  const char* body = sep + 2;
  while (isSpace(*body)) body++;
  size_t body_len = strlen(body);
  while (body_len > 0 && isSpace(body[body_len - 1])) body_len--;

  const char* word = body;
  size_t word_len = body_len;
  if (word_len > 0 && word[0] == '!') {
    word++;
    word_len--;
  }
  if (!equalsLower(word, word_len, "test")) return false;

  size_t sender_len = utf8SafeLen(text, (size_t)(sep - text), cap - 1);
  memcpy(sender_out, text, sender_len);
  sender_out[sender_len] = 0;
  return sender_len > 0;
}

size_t formatHopReply(const char* sender, uint8_t hop_count, const uint8_t* path, uint8_t hash_size, char* out,
                      size_t cap) {
  if (cap == 0) return 0;
  out[0] = 0;
  if (cap == 1) return 0;
  const size_t budget = cap - 1;

  if (hash_size < 1) hash_size = 1;
  if (hash_size > 4) hash_size = 4;

  char tail[40];
  if (hop_count == 0) {
    snprintf(tail, sizeof(tail), "]%s", strings::kBotReplyDirect);
  } else {
    snprintf(tail, sizeof(tail), strings::kBotReplyHopsFmt, (unsigned)hop_count);
  }
  const size_t tail_len = strlen(tail);
  const size_t fixed = 2 + tail_len;  // "@[" + tail

  // Spazio per il nome: se serve lo si accorcia. Con hop si riservano anche
  // i 3 caratteri di "..." per il caso peggiore.
  size_t reserve = fixed + (hop_count > 0 ? 3 : 0);
  size_t sender_room = budget > reserve ? budget - reserve : 0;
  size_t sender_len = utf8SafeLen(sender, strlen(sender), sender_room);

  size_t len = 0;
  auto put = [&](const char* s, size_t n) {
    if (len + n > budget) n = budget - len;
    memcpy(out + len, s, n);
    len += n;
  };
  put("@[", 2);
  put(sender, sender_len);
  put(tail, tail_len);

  if (hop_count > 0) {
    const size_t hop_chars = (size_t)hash_size * 2;
    // Quanti hop interi entrano; se non entrano tutti serve spazio per ",...".
    size_t fit = 0;
    for (size_t k = 1; k <= hop_count; k++) {
      size_t need = len + k * hop_chars + (k - 1);  // hop + virgole
      if (need + (k < hop_count ? 4 : 0) > budget) break;
      fit = k;
    }
    static const char kHex[] = "0123456789abcdef";
    for (size_t k = 0; k < fit; k++) {
      if (k > 0) put(",", 1);
      for (size_t b = 0; b < hash_size; b++) {
        uint8_t v = path[k * hash_size + b];
        char h[2] = {kHex[v >> 4], kHex[v & 0x0F]};
        put(h, 2);
      }
    }
    if (fit < hop_count) put(fit > 0 ? ",..." : "...", fit > 0 ? 4 : 3);
  }
  out[len] = 0;
  return len;
}

// --- Cooldown ---

bool BotCooldown::allow(size_t idx, uint32_t now_ms) {
  if (idx >= kBotMaxChannels) return false;
  if (_used[idx] && (uint32_t)(now_ms - _last[idx]) < kBotCooldownMs) return false;
  _used[idx] = true;
  _last[idx] = now_ms;
  return true;
}

} // namespace bbs
