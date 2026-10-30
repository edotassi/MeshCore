#include "room_post_file.h"

#include <cstring>

#include "bbs_binary_io.h"
#include "bbs_crc16.h"

namespace room {

namespace {

constexpr uint8_t kMagic[4] = {'R', 'P', 'S', 'T'};
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderLen = 4 + 1 + 4;
constexpr size_t kRecFixedLen = kPubKeyLen + 4 + 1;                  // fino alla lunghezza del testo
constexpr size_t kRecMaxLen = kRecFixedLen + kMaxPostTextLen + 2;    // + testo + crc

size_t encodeRecord(const PostRecord& r, uint8_t* buf) {
  size_t tlen = strnlen(r.text, kMaxPostTextLen);
  memcpy(buf, r.pub_key, kPubKeyLen);
  bbs::putU32(buf + kPubKeyLen, r.timestamp);
  buf[kPubKeyLen + 4] = (uint8_t)tlen;
  memcpy(buf + kRecFixedLen, r.text, tlen);
  size_t body = kRecFixedLen + tlen;
  bbs::putU16(buf + body, bbs::crc16Ccitt(buf, body));
  return body + 2;
}

} // namespace

bool PostFile::save(const PostRecord* recs, size_t n) {
  struct ArraySource : IPostSource {
    const PostRecord* recs;
    bool get(size_t i, PostRecord& out) const override {
      out = recs[i];
      return true;
    }
  } src;
  src.recs = recs;
  return save(src, n);
}

bool PostFile::save(const IPostSource& src, size_t n) {
  bbs::IFile* f = _fs.open(kPostsTmpPath, 'w');
  if (!f || !f->valid()) {
    if (f) f->close();
    return false;
  }

  uint8_t buf[kRecMaxLen];
  memcpy(buf, kMagic, 4);
  buf[4] = kVersion;
  bbs::putU32(buf + 5, (uint32_t)n);
  bool ok = (f->write(buf, kHeaderLen) == kHeaderLen);

  PostRecord rec;
  for (size_t i = 0; ok && i < n; i++) {
    if (!src.get(i, rec)) {
      ok = false;
      break;
    }
    size_t len = encodeRecord(rec, buf);
    ok = (f->write(buf, len) == len);
  }
  f->close();

  if (!ok) {
    _fs.remove(kPostsTmpPath);
    return false;
  }
  _fs.remove(kPostsPath);  // assente la prima volta: l'esito non conta
  return _fs.rename(kPostsTmpPath, kPostsPath);
}

size_t PostFile::load(PostRecord* out, size_t cap) {
  if (cap == 0) return 0;
  bbs::IFile* f = _fs.open(kPostsPath, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    // Spegnimento tra remove e rename in save(): resta solo il temporaneo,
    // gia' scritto per intero (un record troncato viene comunque scartato).
    f = _fs.open(kPostsTmpPath, 'r');
    if (!f || !f->valid()) {
      if (f) f->close();
      return 0;
    }
  }

  uint8_t buf[kRecMaxLen];
  size_t count = 0;
  if (f->read(buf, kHeaderLen) == kHeaderLen && memcmp(buf, kMagic, 4) == 0 && buf[4] == kVersion) {
    uint32_t declared = bbs::getU32(buf + 5);
    for (uint32_t i = 0; i < declared; i++) {
      if (f->read(buf, kRecFixedLen) != kRecFixedLen) break;  // troncato
      size_t tlen = buf[kPubKeyLen + 4];
      if (tlen > kMaxPostTextLen) break;  // framing perso: impossibile risincronizzare
      size_t rest = tlen + 2;
      if (f->read(buf + kRecFixedLen, rest) != rest) break;  // troncato

      size_t body = kRecFixedLen + tlen;
      if (bbs::getU16(buf + body) != bbs::crc16Ccitt(buf, body)) continue;  // record corrotto: saltato

      if (count == cap) {  // pieno: scarta il piu' vecchio
        memmove(out, out + 1, (cap - 1) * sizeof(PostRecord));
        count--;
      }
      PostRecord& r = out[count++];
      memcpy(r.pub_key, buf, kPubKeyLen);
      r.timestamp = bbs::getU32(buf + kPubKeyLen);
      memcpy(r.text, buf + kRecFixedLen, tlen);
      r.text[tlen] = 0;
    }
  }
  f->close();
  return count;
}

} // namespace room
