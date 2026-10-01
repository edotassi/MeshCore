#include "bbs_motd.h"

#include <cstring>

namespace bbs {

MotdStore::MotdStore(IFileSystem& fs, const char* path) : _fs(fs), _path(path) {}

bool MotdStore::get(char* out_buf, size_t out_cap) const {
  if (out_cap < 1) return false;
  out_buf[0] = 0;

  IFile* f = _fs.open(_path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return true;  // nessun MOTD impostato, non e' un errore
  }
  size_t len = f->read((uint8_t*)out_buf, out_cap - 1);
  f->close();
  out_buf[len] = 0;
  return true;
}

bool MotdStore::set(const char* text) {
  size_t len = strlen(text);
  if (len > BBS_MAX_TEXT_LEN) len = BBS_MAX_TEXT_LEN;  // difesa, il chiamante dovrebbe gia' rispettare il budget

  IFile* f = _fs.open(_path, 'w');
  if (!f || !f->valid()) {
    if (f) f->close();
    return false;
  }
  bool ok = (f->write((const uint8_t*)text, len) == len);
  f->close();
  return ok;
}

} // namespace bbs
