#include "bbs_news.h"

#include <cstdio>
#include <cstring>

#include "bbs_binary_io.h"
#include "bbs_sha256.h"

namespace bbs {

namespace {

constexpr uint8_t kNewsFileVersion = 2;
// v1: version, enabled, port(2), server[64], username[32], password[32], topic[64],
// key_len, key[32], room_id
constexpr size_t kNewsFileSizeV1 =
    1 + 1 + 2 + kNewsServerLen + kNewsUserLen + kNewsPassLen + kNewsTopicLen + 1 + kNewsKeyMaxLen + 1;
// v2: come v1, poi channel_enabled, channel.name[24], channel.psk_len, channel.psk[32]
constexpr size_t kNewsFileSize = kNewsFileSizeV1 + 1 + kBotNameLen + 1 + kBotPskMaxLen;

constexpr uint8_t kSeenFileVersion = 1;
// version, count, head, hash[64][8]
constexpr size_t kSeenFileSize = 1 + 1 + 1 + kNewsSeenCapacity * kNewsSeenHashLen;

bool writeFileAtomically(IFileSystem& fs, const char* path, const uint8_t* buf, size_t len) {
  char tmp_path[40];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
  IFile* dst = fs.open(tmp_path, 'w');
  if (!dst || !dst->valid()) {
    if (dst) dst->close();
    return false;
  }
  bool ok = (dst->write(buf, len) == len);
  dst->close();
  if (!ok) {
    fs.remove(tmp_path);
    return false;
  }
  fs.remove(path);
  return fs.rename(tmp_path, path);
}

bool terminated(const char* s, size_t cap) { return s[cap - 1] == 0; }

bool isContinuationByte(char c) { return ((uint8_t)c & 0xC0) == 0x80; }

// Il piu' grande n <= max tale che il taglio a n byte non spezzi un code point.
size_t utf8SafeLen(const char* s, size_t len, size_t max) {
  if (len <= max) return len;
  size_t n = max;
  while (n > 0 && isContinuationByte(s[n])) n--;
  return n;
}

// UTF-8 ben formato (struttura dei byte; non controlla le sequenze
// sovra-lunghe, che qui sono innocue).
bool isWellFormedUtf8(const char* s, size_t len) {
  size_t i = 0;
  while (i < len) {
    uint8_t c = (uint8_t)s[i];
    size_t extra;
    if (c < 0x80) extra = 0;
    else if (c >= 0xC2 && c <= 0xDF) extra = 1;
    else if (c >= 0xE0 && c <= 0xEF) extra = 2;
    else if (c >= 0xF0 && c <= 0xF4) extra = 3;
    else return false;
    if (i + extra >= len) return false;  // sequenza troncata
    for (size_t k = 1; k <= extra; k++) {
      if (!isContinuationByte(s[i + k])) return false;
    }
    i += extra + 1;
  }
  return true;
}

// Testo senza controlli (a capo, tab, ...) e UTF-8 valido.
bool isCleanText(const char* s, size_t len) {
  for (size_t i = 0; i < len; i++) {
    uint8_t c = (uint8_t)s[i];
    if (c < 0x20 || c == 0x7F) return false;
  }
  return isWellFormedUtf8(s, len);
}

bool isIdChar(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  return -1;
}

} // namespace

// --- NewsSettings ---

NewsSettings::NewsSettings()
    : enabled(false), server{}, port(kNewsDefaultPort), username{}, password{}, topic{}, key{}, key_len(0),
      room_id(kNoRoom), channel(), channel_enabled(false) {
  strcpy(topic, kNewsDefaultTopic);
}

bool newsChannelValid(const NewsSettings& s) {
  const BotChannel& c = s.channel;
  if (c.name[kBotNameLen - 1] != 0) return false;
  if (c.psk_len != 0 && c.psk_len != 16 && c.psk_len != 32) return false;
  if (c.name[0] == 0) return !s.channel_enabled && c.psk_len == 0;
  if (c.psk_len == 0) {
    char norm[kBotNameLen];
    return normalizeHashtagName(c.name, norm, sizeof(norm)) && strcmp(norm, c.name) == 0;
  }
  return isValidKeyedName(c.name);
}

const char* newsResultName(NewsIngestResult r) {
  switch (r) {
    case NewsIngestResult::OK: return "OK";
    case NewsIngestResult::NOT_ENABLED: return "NOT_ENABLED";
    case NewsIngestResult::NO_KEY: return "NO_KEY";
    case NewsIngestResult::NO_ROOM: return "NO_ROOM";
    case NewsIngestResult::BAD_FORMAT: return "BAD_FORMAT";
    case NewsIngestResult::BAD_SIGNATURE: return "BAD_SIGNATURE";
    case NewsIngestResult::DUPLICATE: return "DUPLICATE";
    case NewsIngestResult::STALE: return "STALE";
    case NewsIngestResult::IO_ERROR: return "IO_ERROR";
  }
  return "?";
}

// --- NewsStore ---

NewsStore::NewsStore(IFileSystem& fs, const char* path) : _fs(fs), _path(path) { load(); }

void NewsStore::load() {
  _settings = NewsSettings();

  IFile* f = _fs.open(_path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return;
  }
  uint8_t buf[kNewsFileSize];
  size_t n = f->read(buf, sizeof(buf));
  f->close();
  // v1 (senza canale) resta leggibile: tutti i campi si conservano e il
  // canale resta spento; al prossimo salvataggio il file diventa v2.
  bool v1 = (n == kNewsFileSizeV1 && buf[0] == 1);
  if (!v1 && (n != sizeof(buf) || buf[0] != kNewsFileVersion)) return;

  NewsSettings s;
  size_t o = 1;
  s.enabled = buf[o++] != 0;
  s.port = getU16(&buf[o]);
  o += 2;
  memcpy(s.server, &buf[o], kNewsServerLen);
  o += kNewsServerLen;
  memcpy(s.username, &buf[o], kNewsUserLen);
  o += kNewsUserLen;
  memcpy(s.password, &buf[o], kNewsPassLen);
  o += kNewsPassLen;
  memcpy(s.topic, &buf[o], kNewsTopicLen);
  o += kNewsTopicLen;
  s.key_len = buf[o++];
  memcpy(s.key, &buf[o], kNewsKeyMaxLen);
  o += kNewsKeyMaxLen;
  s.room_id = buf[o++];
  if (!v1) {
    s.channel_enabled = buf[o++] != 0;
    memcpy(s.channel.name, &buf[o], kBotNameLen);
    o += kBotNameLen;
    s.channel.psk_len = buf[o++];
    memcpy(s.channel.psk, &buf[o], kBotPskMaxLen);
    o += kBotPskMaxLen;
  }

  if (s.port == 0) return;
  if (!newsChannelValid(s)) return;
  if (!terminated(s.server, sizeof(s.server)) || !terminated(s.username, sizeof(s.username)) ||
      !terminated(s.password, sizeof(s.password)) || !terminated(s.topic, sizeof(s.topic))) {
    return;
  }
  if (s.topic[0] == 0) return;
  if (s.key_len != 0 && (s.key_len < kNewsKeyMinLen || s.key_len > kNewsKeyMaxLen)) return;
  if (s.room_id != kNoRoom && s.room_id >= BBS_MAX_ROOMS) return;
  _settings = s;
}

bool NewsStore::save(const NewsSettings& s) {
  if (s.port == 0 || s.topic[0] == 0) return false;
  if (s.key_len != 0 && (s.key_len < kNewsKeyMinLen || s.key_len > kNewsKeyMaxLen)) return false;
  if (s.room_id != kNoRoom && s.room_id >= BBS_MAX_ROOMS) return false;
  if (!newsChannelValid(s)) return false;

  uint8_t buf[kNewsFileSize];
  memset(buf, 0, sizeof(buf));
  size_t o = 0;
  buf[o++] = kNewsFileVersion;
  buf[o++] = s.enabled ? 1 : 0;
  putU16(&buf[o], s.port);
  o += 2;
  memcpy(&buf[o], s.server, kNewsServerLen);
  o += kNewsServerLen;
  memcpy(&buf[o], s.username, kNewsUserLen);
  o += kNewsUserLen;
  memcpy(&buf[o], s.password, kNewsPassLen);
  o += kNewsPassLen;
  memcpy(&buf[o], s.topic, kNewsTopicLen);
  o += kNewsTopicLen;
  buf[o++] = s.key_len;
  memcpy(&buf[o], s.key, kNewsKeyMaxLen);
  o += kNewsKeyMaxLen;
  buf[o++] = s.room_id;
  buf[o++] = s.channel_enabled ? 1 : 0;
  memcpy(&buf[o], s.channel.name, kBotNameLen);
  o += kBotNameLen;
  buf[o++] = s.channel.psk_len;
  memcpy(&buf[o], s.channel.psk, kBotPskMaxLen);
  o += kBotPskMaxLen;

  if (!writeFileAtomically(_fs, _path, buf, sizeof(buf))) return false;

  _settings = s;  // persisti-poi-applica
  _generation++;
  return true;
}

void NewsStore::recordResult(NewsIngestResult r, uint32_t now_ts) {
  _status.has_result = true;
  _status.last_result = r;
  if (r == NewsIngestResult::OK) {
    _status.received_ok++;
    _status.last_ok_ts = now_ts;
  }
}

// --- NewsSeen ---

NewsSeen::NewsSeen(IFileSystem& fs, const char* path) : _fs(fs), _path(path) {
  memset(_hashes, 0, sizeof(_hashes));
  load();
}

void NewsSeen::hashId(const char* id, uint8_t out[kNewsSeenHashLen]) {
  uint8_t digest[kSha256DigestLen];
  sha256((const uint8_t*)id, strlen(id), digest);
  memcpy(out, digest, kNewsSeenHashLen);
}

void NewsSeen::load() {
  IFile* f = _fs.open(_path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return;
  }
  uint8_t buf[kSeenFileSize];
  size_t n = f->read(buf, sizeof(buf));
  f->close();
  if (n != sizeof(buf) || buf[0] != kSeenFileVersion) return;
  uint8_t count = buf[1];
  uint8_t head = buf[2];
  if (count > kNewsSeenCapacity || head >= kNewsSeenCapacity) return;
  if (count < kNewsSeenCapacity && head != count) return;  // anello non ancora pieno: si riempie da 0

  memcpy(_hashes, &buf[3], sizeof(_hashes));
  _count = count;
  _head = head;
}

bool NewsSeen::persist() const {
  uint8_t buf[kSeenFileSize];
  buf[0] = kSeenFileVersion;
  buf[1] = _count;
  buf[2] = _head;
  memcpy(&buf[3], _hashes, sizeof(_hashes));
  return writeFileAtomically(_fs, _path, buf, sizeof(buf));
}

bool NewsSeen::containsHash(const uint8_t h[kNewsSeenHashLen]) const {
  for (size_t i = 0; i < _count; i++) {
    if (memcmp(_hashes[i], h, kNewsSeenHashLen) == 0) return true;
  }
  return false;
}

bool NewsSeen::contains(const char* id) const {
  uint8_t h[kNewsSeenHashLen];
  hashId(id, h);
  return containsHash(h);
}

bool NewsSeen::add(const char* id) {
  uint8_t h[kNewsSeenHashLen];
  hashId(id, h);
  if (containsHash(h)) return true;

  memcpy(_hashes[_head], h, kNewsSeenHashLen);
  _head = (uint8_t)((_head + 1) % kNewsSeenCapacity);
  if (_count < kNewsSeenCapacity) _count++;
  return persist();
}

// --- Parsing e verifica ---

bool parseNewsMessage(const char* payload, size_t len, NewsMessage& out) {
  if (payload == nullptr || len == 0 || len > kNewsMaxPayloadLen) return false;
  if (memchr(payload, 0, len) != nullptr) return false;
  if (payload[len - 1] == '\n') len--;  // un solo a capo finale tollerato

  // Esattamente 6 righe.
  const char* lines[6];
  size_t lens[6];
  size_t n_lines = 0;
  size_t start = 0;
  for (size_t i = 0; i <= len; i++) {
    if (i == len || payload[i] == '\n') {
      if (n_lines == 6) return false;
      lines[n_lines] = payload + start;
      lens[n_lines] = i - start;
      n_lines++;
      start = i + 1;
    }
  }
  if (n_lines != 6) return false;

  if (lens[0] != 2 || memcmp(lines[0], "v1", 2) != 0) return false;

  // id
  if (lens[1] < 1 || lens[1] > kNewsIdMaxLen) return false;
  for (size_t i = 0; i < lens[1]; i++) {
    if (!isIdChar(lines[1][i])) return false;
  }

  // ts: solo cifre, forma canonica (niente zeri iniziali), entra in 32 bit.
  if (lens[2] < 1 || lens[2] > 10) return false;
  if (lens[2] > 1 && lines[2][0] == '0') return false;
  uint64_t ts = 0;
  for (size_t i = 0; i < lens[2]; i++) {
    if (lines[2][i] < '0' || lines[2][i] > '9') return false;
    ts = ts * 10 + (uint64_t)(lines[2][i] - '0');
  }
  if (ts > 0xFFFFFFFFull) return false;

  if (lens[3] < 1 || lens[3] > kNewsSourceMaxLen || !isCleanText(lines[3], lens[3])) return false;
  if (lens[4] < 1 || lens[4] > kNewsTitleMaxLen || !isCleanText(lines[4], lens[4])) return false;

  if (lens[5] != 64) return false;
  uint8_t mac[32];
  for (size_t i = 0; i < 32; i++) {
    int hi = hexVal(lines[5][i * 2]);
    int lo = hexVal(lines[5][i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    mac[i] = (uint8_t)((hi << 4) | lo);
  }

  memcpy(out.id, lines[1], lens[1]);
  out.id[lens[1]] = 0;
  out.ts = (uint32_t)ts;
  memcpy(out.source, lines[3], lens[3]);
  out.source[lens[3]] = 0;
  memcpy(out.title, lines[4], lens[4]);
  out.title[lens[4]] = 0;
  memcpy(out.hmac, mac, sizeof(mac));
  return true;
}

bool verifyNews(const NewsMessage& msg, const uint8_t* key, size_t key_len) {
  if (key == nullptr || key_len == 0) return false;
  // La forma di ts e' canonica (vedi parseNewsMessage), quindi il messaggio
  // firmato si ricostruisce identico a quello ricevuto.
  char signed_text[4 + kNewsIdMaxLen + 1 + 10 + 1 + kNewsSourceMaxLen + 1 + kNewsTitleMaxLen + 1];
  int n = snprintf(signed_text, sizeof(signed_text), "v1\n%s\n%lu\n%s\n%s", msg.id, (unsigned long)msg.ts,
                   msg.source, msg.title);
  if (n <= 0 || (size_t)n >= sizeof(signed_text)) return false;

  uint8_t expected[kSha256DigestLen];
  hmacSha256(key, key_len, (const uint8_t*)signed_text, (size_t)n, expected);
  return constantTimeEqual(expected, msg.hmac, sizeof(expected));
}

// --- Accodamento ---

NewsIngestResult ingestNews(NewsStore& news, NewsSeen& seen, PostStore& posts, RoomRegistry& rooms,
                            UserStore& users, UserId system_user, const char* payload, size_t len,
                            uint32_t now_ts, bool clock_valid, char* posted_text) {
  auto done = [&](NewsIngestResult r) {
    news.recordResult(r, now_ts);
    return r;
  };

  const NewsSettings& s = news.get();
  if (!s.enabled) return done(NewsIngestResult::NOT_ENABLED);
  if (s.key_len == 0) return done(NewsIngestResult::NO_KEY);
  if (s.room_id == kNoRoom || rooms.findRoom(s.room_id) == nullptr) return done(NewsIngestResult::NO_ROOM);

  NewsMessage msg;
  if (!parseNewsMessage(payload, len, msg)) return done(NewsIngestResult::BAD_FORMAT);
  if (!verifyNews(msg, s.key, s.key_len)) return done(NewsIngestResult::BAD_SIGNATURE);

  if (clock_valid) {
    if ((uint64_t)msg.ts + kNewsMaxAgeSecs < (uint64_t)now_ts) return done(NewsIngestResult::STALE);
    if ((uint64_t)msg.ts > (uint64_t)now_ts + kNewsMaxFutureSecs) return done(NewsIngestResult::STALE);
  }
  if (seen.contains(msg.id)) return done(NewsIngestResult::DUPLICATE);

  uint8_t author_key[BBS_PUBKEY_LEN];
  if (system_user == kInvalidUserId || !users.getPubkey(system_user, author_key)) {
    return done(NewsIngestResult::IO_ERROR);
  }

  char text[BBS_MAX_TEXT_LEN + 1 + 32];
  int n = snprintf(text, sizeof(text), "[%s] %s", msg.source, msg.title);
  if (n <= 0) return done(NewsIngestResult::BAD_FORMAT);
  size_t text_len = utf8SafeLen(text, (size_t)n < sizeof(text) ? (size_t)n : sizeof(text) - 1, BBS_MAX_TEXT_LEN);
  text[text_len] = 0;

  if (!posts.appendPost(s.room_id, system_user, now_ts, text)) return done(NewsIngestResult::IO_ERROR);
  posts.enforceRetention(s.room_id, BBS_MAX_POSTS_PER_ROOM);
  seen.add(msg.id);
  if (posted_text != nullptr) memcpy(posted_text, text, text_len + 1);
  return done(NewsIngestResult::OK);
}

} // namespace bbs
