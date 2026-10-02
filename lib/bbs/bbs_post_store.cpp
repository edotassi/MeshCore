#include "bbs_post_store.h"

#include <cstdio>
#include <cstring>

#include "bbs_binary_io.h"
#include "bbs_crc16.h"

namespace bbs {

PostStore::PostStore(IFileSystem& fs) : _fs(fs) {}

void PostStore::roomPath(uint8_t room_id, char* out, size_t cap) {
  snprintf(out, cap, "/bbs/r%02u.log", (unsigned)room_id);
}

bool PostStore::readRecord(IFile& f, PostRecord& out) const {
  uint8_t header[BBS_POST_HEADER_SIZE];
  if (f.read(header, sizeof(header)) != sizeof(header)) return false;

  size_t n = 0;
  out.version = header[n++];
  out.room_id = header[n++];
  out.timestamp = getU32(header + n);
  n += 4;
  out.user_id = getU32(header + n);
  n += 4;
  out.text_len = getU16(header + n);
  n += 2;
  out.flags = header[n++];

  if (out.version != BBS_POST_RECORD_VERSION) return false;
  if (out.text_len > BBS_MAX_TEXT_LEN) return false;

  if (f.read((uint8_t*)out.text, out.text_len) != out.text_len) return false;
  out.text[out.text_len] = 0;

  uint8_t crcbuf[2];
  if (f.read(crcbuf, 2) != 2) return false;
  uint16_t stored_crc = getU16(crcbuf);

  uint8_t tmp[BBS_POST_HEADER_SIZE + BBS_MAX_TEXT_LEN];
  memcpy(tmp, header, BBS_POST_HEADER_SIZE);
  memcpy(tmp + BBS_POST_HEADER_SIZE, out.text, out.text_len);
  uint16_t calc_crc = crc16Ccitt(tmp, BBS_POST_HEADER_SIZE + out.text_len);

  return calc_crc == stored_crc;
}

bool PostStore::writeRecord(IFile& f, const PostRecord& rec) const {
  uint8_t buf[BBS_POST_HEADER_SIZE + BBS_MAX_TEXT_LEN];
  size_t n = 0;
  buf[n++] = rec.version;
  buf[n++] = rec.room_id;
  putU32(buf + n, rec.timestamp);
  n += 4;
  putU32(buf + n, rec.user_id);
  n += 4;
  putU16(buf + n, rec.text_len);
  n += 2;
  buf[n++] = rec.flags;
  memcpy(buf + n, rec.text, rec.text_len);
  n += rec.text_len;

  uint16_t crc = crc16Ccitt(buf, n);

  bool ok = (f.write(buf, n) == n);
  uint8_t crcbuf[2];
  putU16(crcbuf, crc);
  ok = ok && (f.write(crcbuf, 2) == 2);
  return ok;
}

bool PostStore::appendPost(uint8_t room_id, UserId author, uint32_t timestamp, const char* text) {
  size_t text_len = strlen(text);
  if (room_id >= BBS_MAX_ROOMS || text_len == 0 || text_len > BBS_MAX_TEXT_LEN) return false;

  PostRecord rec;
  rec.version = BBS_POST_RECORD_VERSION;
  rec.room_id = room_id;
  rec.timestamp = timestamp;
  rec.user_id = author;
  rec.text_len = (uint16_t)text_len;
  rec.flags = 0;
  memcpy(rec.text, text, text_len);
  rec.text[text_len] = 0;

  char path[16];
  roomPath(room_id, path, sizeof(path));
  IFile* f = _fs.open(path, 'a');
  if (!f || !f->valid()) {
    if (f) f->close();
    return false;
  }
  bool ok = writeRecord(*f, rec);
  f->close();
  return ok;
}

bool PostStore::findNextUnread(uint8_t room_id, uint32_t after_ts, PostRecord& out) const {
  if (room_id >= BBS_MAX_ROOMS) return false;

  char path[16];
  roomPath(room_id, path, sizeof(path));
  IFile* f = _fs.open(path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return false;
  }

  bool found = false;
  PostRecord rec;
  while (readRecord(*f, rec)) {
    if (rec.flags & kPostFlagDeleted) continue;
    if (rec.timestamp > after_ts) {
      out = rec;
      found = true;
      break;
    }
  }
  f->close();
  return found;
}

uint32_t PostStore::countUnread(uint8_t room_id, uint32_t after_ts) const {
  if (room_id >= BBS_MAX_ROOMS) return 0;

  char path[16];
  roomPath(room_id, path, sizeof(path));
  IFile* f = _fs.open(path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return 0;
  }

  uint32_t count = 0;
  PostRecord rec;
  while (readRecord(*f, rec)) {
    if (rec.flags & kPostFlagDeleted) continue;
    if (rec.timestamp > after_ts) count++;
  }
  f->close();
  return count;
}

bool PostStore::repairRoom(uint8_t room_id) {
  if (room_id >= BBS_MAX_ROOMS) return false;

  char path[16];
  roomPath(room_id, path, sizeof(path));
  IFile* src = _fs.open(path, 'r');
  if (!src || !src->valid()) {
    if (src) src->close();
    return true; // niente da riparare, il file non esiste ancora
  }

  char tmp_path[24];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
  IFile* dst = _fs.open(tmp_path, 'w');
  if (!dst || !dst->valid()) {
    src->close();
    if (dst) dst->close();
    return false;
  }

  bool ok = true;
  PostRecord rec;
  while (readRecord(*src, rec)) {
    if (!writeRecord(*dst, rec)) {
      ok = false;
      break;
    }
  }
  src->close();
  dst->close();

  if (!ok) {
    _fs.remove(tmp_path);
    return false;
  }

  _fs.remove(path);
  return _fs.rename(tmp_path, path);
}

bool PostStore::deleteLastPost(uint8_t room_id) {
  if (room_id >= BBS_MAX_ROOMS) return false;

  char path[16];
  roomPath(room_id, path, sizeof(path));

  // Primo giro: conta i record (non possiamo bufferizzarli tutti, nessuna
  // allocazione dinamica ammessa).
  IFile* src = _fs.open(path, 'r');
  if (!src || !src->valid()) {
    if (src) src->close();
    return false;
  }
  uint32_t total = 0;
  PostRecord tmp;
  while (readRecord(*src, tmp)) total++;
  src->close();
  if (total == 0) return false;

  // Secondo giro: ricopia tutto, marcando cancellato solo l'ultimo.
  src = _fs.open(path, 'r');
  if (!src || !src->valid()) {
    if (src) src->close();
    return false;
  }
  char tmp_path[24];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
  IFile* dst = _fs.open(tmp_path, 'w');
  if (!dst || !dst->valid()) {
    src->close();
    if (dst) dst->close();
    return false;
  }

  bool ok = true;
  uint32_t idx = 0;
  PostRecord rec;
  while (ok && readRecord(*src, rec)) {
    if (idx == total - 1) rec.flags |= kPostFlagDeleted;
    ok = writeRecord(*dst, rec);
    idx++;
  }
  src->close();
  dst->close();

  if (!ok) {
    _fs.remove(tmp_path);
    return false;
  }

  _fs.remove(path);
  return _fs.rename(tmp_path, path);
}

bool PostStore::enforceRetention(uint8_t room_id, uint32_t max_records) {
  if (room_id >= BBS_MAX_ROOMS) return false;

  char path[16];
  roomPath(room_id, path, sizeof(path));

  IFile* src = _fs.open(path, 'r');
  if (!src || !src->valid()) {
    if (src) src->close();
    return true;  // niente da fare, la stanza non ha nemmeno un log
  }
  uint32_t total = 0;
  PostRecord tmp;
  while (readRecord(*src, tmp)) total++;
  src->close();

  if (total <= max_records) return true;  // gia' sotto il limite

  uint32_t to_skip = total - max_records;

  src = _fs.open(path, 'r');
  if (!src || !src->valid()) {
    if (src) src->close();
    return false;
  }
  char tmp_path[24];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
  IFile* dst = _fs.open(tmp_path, 'w');
  if (!dst || !dst->valid()) {
    src->close();
    if (dst) dst->close();
    return false;
  }

  bool ok = true;
  uint32_t idx = 0;
  PostRecord rec;
  while (ok && readRecord(*src, rec)) {
    if (idx >= to_skip) ok = writeRecord(*dst, rec);  // scarta i piu' vecchi
    idx++;
  }
  src->close();
  dst->close();

  if (!ok) {
    _fs.remove(tmp_path);
    return false;
  }

  _fs.remove(path);
  return _fs.rename(tmp_path, path);
}

bool PostStore::purgeRoom(uint8_t room_id) {
  if (room_id >= BBS_MAX_ROOMS) return false;
  char path[16];
  roomPath(room_id, path, sizeof(path));
  _fs.remove(path);  // no-op se il file non esiste
  return true;
}

bool PostStore::pinLastPost(uint8_t room_id) {
  if (room_id >= BBS_MAX_ROOMS) return false;

  char path[16];
  roomPath(room_id, path, sizeof(path));

  IFile* src = _fs.open(path, 'r');
  if (!src || !src->valid()) {
    if (src) src->close();
    return false;
  }
  uint32_t total = 0;
  PostRecord tmp;
  while (readRecord(*src, tmp)) total++;
  src->close();
  if (total == 0) return false;

  src = _fs.open(path, 'r');
  if (!src || !src->valid()) {
    if (src) src->close();
    return false;
  }
  char tmp_path[24];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
  IFile* dst = _fs.open(tmp_path, 'w');
  if (!dst || !dst->valid()) {
    src->close();
    if (dst) dst->close();
    return false;
  }

  bool ok = true;
  uint32_t idx = 0;
  PostRecord rec;
  while (ok && readRecord(*src, rec)) {
    rec.flags &= (uint8_t)~kPostFlagPinned;  // al piu' un post fissato per stanza
    if (idx == total - 1) rec.flags |= kPostFlagPinned;
    ok = writeRecord(*dst, rec);
    idx++;
  }
  src->close();
  dst->close();

  if (!ok) {
    _fs.remove(tmp_path);
    return false;
  }

  _fs.remove(path);
  return _fs.rename(tmp_path, path);
}

bool PostStore::unpinRoom(uint8_t room_id) {
  if (room_id >= BBS_MAX_ROOMS) return false;

  char path[16];
  roomPath(room_id, path, sizeof(path));

  IFile* src = _fs.open(path, 'r');
  if (!src || !src->valid()) {
    if (src) src->close();
    return false;  // niente da sfissare
  }

  char tmp_path[24];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
  IFile* dst = _fs.open(tmp_path, 'w');
  if (!dst || !dst->valid()) {
    src->close();
    if (dst) dst->close();
    return false;
  }

  bool ok = true;
  bool had_pinned = false;
  PostRecord rec;
  while (ok && readRecord(*src, rec)) {
    if (rec.flags & kPostFlagPinned) {
      rec.flags &= (uint8_t)~kPostFlagPinned;
      had_pinned = true;
    }
    ok = writeRecord(*dst, rec);
  }
  src->close();
  dst->close();

  if (!ok) {
    _fs.remove(tmp_path);
    return false;
  }

  _fs.remove(path);
  if (!_fs.rename(tmp_path, path)) return false;
  return had_pinned;
}

bool PostStore::findPinned(uint8_t room_id, PostRecord& out) const {
  if (room_id >= BBS_MAX_ROOMS) return false;

  char path[16];
  roomPath(room_id, path, sizeof(path));
  IFile* f = _fs.open(path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return false;
  }

  bool found = false;
  PostRecord rec;
  while (readRecord(*f, rec)) {
    if ((rec.flags & kPostFlagDeleted) == 0 && (rec.flags & kPostFlagPinned) != 0) {
      out = rec;
      found = true;
    }
  }
  f->close();
  return found;
}

bool PostStore::searchRecent(uint8_t room_id, const char* needle, uint32_t max_scan, PostRecord& out) const {
  if (room_id >= BBS_MAX_ROOMS || needle[0] == 0) return false;

  char path[16];
  roomPath(room_id, path, sizeof(path));

  IFile* f = _fs.open(path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return false;
  }
  uint32_t total = 0;
  PostRecord tmp;
  while (readRecord(*f, tmp)) total++;
  f->close();
  if (total == 0) return false;

  uint32_t start_idx = (total > max_scan) ? (total - max_scan) : 0;

  f = _fs.open(path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return false;
  }
  bool found = false;
  uint32_t idx = 0;
  PostRecord rec;
  while (readRecord(*f, rec)) {
    if (idx >= start_idx && !(rec.flags & kPostFlagDeleted) && strstr(rec.text, needle) != nullptr) {
      out = rec;
      found = true;  // non interrompe: si cerca il piu' recente, non il primo
    }
    idx++;
  }
  f->close();
  return found;
}

} // namespace bbs
