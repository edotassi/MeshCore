#include "room_motd_seen.h"

#include <cstring>

#include "bbs_binary_io.h"
#include "bbs_crc16.h"

namespace room {

namespace {
constexpr uint8_t kMagic[4] = {'R', 'M', 'O', 'T'};
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderLen = 4 + 1 + 2;                       // magic, versione, conteggio u16
constexpr size_t kEntryLen = kMotdKeyPrefixLen + 4 + 4 + 2;    // + crc16
}

uint32_t motdHash(const char* text) {
  uint32_t h = 2166136261u;
  for (const uint8_t* p = (const uint8_t*)text; *p; p++) {
    h ^= *p;
    h *= 16777619u;
  }
  return h;
}

int MotdSeen::find(const uint8_t* key_prefix) const {
  for (size_t i = 0; i < _count; i++) {
    if (memcmp(_entries[i].key, key_prefix, kMotdKeyPrefixLen) == 0) return (int)i;
  }
  return -1;
}

bool MotdSeen::shouldSend(const uint8_t* key_prefix, uint32_t motd_hash, uint32_t now) const {
  int i = find(key_prefix);
  if (i < 0) return true;
  const Entry& e = _entries[i];
  if (e.hash != motd_hash) return true;
  return now >= e.last_sent && now - e.last_sent >= kMotdResendSecs;
}

void MotdSeen::markSent(const uint8_t* key_prefix, uint32_t motd_hash, uint32_t now) {
  int i = find(key_prefix);
  if (i < 0) {
    if (_count < kMotdSeenCapacity) {
      i = (int)_count++;
    } else {
      i = 0;
      for (size_t k = 1; k < _count; k++) {
        if (_entries[k].last_sent < _entries[i].last_sent) i = (int)k;
      }
    }
    memcpy(_entries[i].key, key_prefix, kMotdKeyPrefixLen);
  }
  _entries[i].hash = motd_hash;
  _entries[i].last_sent = now;
  _dirty = true;
}

void MotdSeen::clear() {
  _count = 0;
  _dirty = true;
}

bool MotdSeen::loadFrom(const char* path) {
  bbs::IFile* f = _fs.open(path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return false;
  }
  uint8_t buf[kEntryLen];
  _count = 0;
  if (f->read(buf, kHeaderLen) == kHeaderLen && memcmp(buf, kMagic, 4) == 0 && buf[4] == kVersion) {
    size_t declared = bbs::getU16(buf + 5);
    for (size_t i = 0; i < declared && _count < kMotdSeenCapacity; i++) {
      if (f->read(buf, kEntryLen) != kEntryLen) break;  // troncato
      size_t body = kEntryLen - 2;
      if (bbs::getU16(buf + body) != bbs::crc16Ccitt(buf, body)) continue;
      Entry& e = _entries[_count++];
      memcpy(e.key, buf, kMotdKeyPrefixLen);
      e.hash = bbs::getU32(buf + 8);
      e.last_sent = bbs::getU32(buf + 12);
    }
  }
  f->close();
  return true;
}

void MotdSeen::load() {
  _count = 0;
  _dirty = false;
  if (!loadFrom(kMotdSeenPath)) loadFrom(kMotdSeenTmpPath);  // spegnimento tra remove e rename
}

bool MotdSeen::save() {
  bbs::IFile* f = _fs.open(kMotdSeenTmpPath, 'w');
  if (!f || !f->valid()) {
    if (f) f->close();
    return false;
  }
  uint8_t buf[kEntryLen];
  memcpy(buf, kMagic, 4);
  buf[4] = kVersion;
  bbs::putU16(buf + 5, (uint16_t)_count);
  bool ok = (f->write(buf, kHeaderLen) == kHeaderLen);
  for (size_t i = 0; ok && i < _count; i++) {
    memcpy(buf, _entries[i].key, kMotdKeyPrefixLen);
    bbs::putU32(buf + 8, _entries[i].hash);
    bbs::putU32(buf + 12, _entries[i].last_sent);
    bbs::putU16(buf + 16, bbs::crc16Ccitt(buf, 16));
    ok = (f->write(buf, kEntryLen) == kEntryLen);
  }
  f->close();
  if (!ok) {
    _fs.remove(kMotdSeenTmpPath);
    return false;
  }
  _fs.remove(kMotdSeenPath);
  if (!_fs.rename(kMotdSeenTmpPath, kMotdSeenPath)) return false;
  _dirty = false;
  return true;
}

} // namespace room
