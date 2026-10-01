#include "bbs_mail_store.h"

#include <cstdio>
#include <cstring>

#include "bbs_binary_io.h"
#include "bbs_crc16.h"

namespace bbs {

MailStore::MailStore(IFileSystem& fs, const char* path) : _fs(fs), _path(path) {}

bool MailStore::readRecord(IFile& f, MailRecord& out) const {
  uint8_t header[BBS_MAIL_HEADER_SIZE];
  if (f.read(header, sizeof(header)) != sizeof(header)) return false;

  size_t n = 0;
  out.version = header[n++];
  out.sender_id = getU32(header + n);
  n += 4;
  out.recipient_id = getU32(header + n);
  n += 4;
  out.timestamp = getU32(header + n);
  n += 4;
  out.text_len = getU16(header + n);
  n += 2;
  out.flags = header[n++];

  if (out.version != BBS_MAIL_RECORD_VERSION) return false;
  if (out.text_len > BBS_MAX_TEXT_LEN) return false;

  if (f.read((uint8_t*)out.text, out.text_len) != out.text_len) return false;
  out.text[out.text_len] = 0;

  uint8_t crcbuf[2];
  if (f.read(crcbuf, 2) != 2) return false;
  uint16_t stored_crc = getU16(crcbuf);

  uint8_t tmp[BBS_MAIL_HEADER_SIZE + BBS_MAX_TEXT_LEN];
  memcpy(tmp, header, BBS_MAIL_HEADER_SIZE);
  memcpy(tmp + BBS_MAIL_HEADER_SIZE, out.text, out.text_len);
  uint16_t calc_crc = crc16Ccitt(tmp, BBS_MAIL_HEADER_SIZE + out.text_len);

  return calc_crc == stored_crc;
}

bool MailStore::writeRecord(IFile& f, const MailRecord& rec) const {
  uint8_t buf[BBS_MAIL_HEADER_SIZE + BBS_MAX_TEXT_LEN];
  size_t n = 0;
  buf[n++] = rec.version;
  putU32(buf + n, rec.sender_id);
  n += 4;
  putU32(buf + n, rec.recipient_id);
  n += 4;
  putU32(buf + n, rec.timestamp);
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

bool MailStore::send(UserId sender, UserId recipient, uint32_t timestamp, const char* text) {
  size_t text_len = strlen(text);
  if (text_len == 0 || text_len > BBS_MAX_TEXT_LEN) return false;

  MailRecord rec;
  rec.version = BBS_MAIL_RECORD_VERSION;
  rec.sender_id = sender;
  rec.recipient_id = recipient;
  rec.timestamp = timestamp;
  rec.text_len = (uint16_t)text_len;
  rec.flags = 0;
  memcpy(rec.text, text, text_len);
  rec.text[text_len] = 0;

  IFile* f = _fs.open(_path, 'a');
  if (!f || !f->valid()) {
    if (f) f->close();
    return false;
  }
  bool ok = writeRecord(*f, rec);
  f->close();
  return ok;
}

bool MailStore::findNextUnread(UserId recipient, uint32_t after_ts, MailRecord& out) const {
  IFile* f = _fs.open(_path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return false;
  }

  bool found = false;
  MailRecord rec;
  while (readRecord(*f, rec)) {
    if (rec.recipient_id == recipient && rec.timestamp > after_ts) {
      out = rec;
      found = true;
      break;
    }
  }
  f->close();
  return found;
}

uint32_t MailStore::countUnread(UserId recipient, uint32_t after_ts) const {
  IFile* f = _fs.open(_path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return 0;
  }

  uint32_t count = 0;
  MailRecord rec;
  while (readRecord(*f, rec)) {
    if (rec.recipient_id == recipient && rec.timestamp > after_ts) count++;
  }
  f->close();
  return count;
}

uint32_t MailStore::totalCount() const {
  IFile* f = _fs.open(_path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return 0;
  }
  uint32_t count = 0;
  MailRecord rec;
  while (readRecord(*f, rec)) count++;
  f->close();
  return count;
}

bool MailStore::repair() {
  IFile* src = _fs.open(_path, 'r');
  if (!src || !src->valid()) {
    if (src) src->close();
    return true; // niente da riparare, il file non esiste ancora
  }

  char tmp_path[32];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", _path);
  IFile* dst = _fs.open(tmp_path, 'w');
  if (!dst || !dst->valid()) {
    src->close();
    if (dst) dst->close();
    return false;
  }

  bool ok = true;
  MailRecord rec;
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

  _fs.remove(_path);
  return _fs.rename(tmp_path, _path);
}

} // namespace bbs
