#include "bbs_pending_welcome.h"

#include <cstring>

namespace bbs {

PendingWelcomeTable::PendingWelcomeTable(IFileSystem& fs, const char* path) : _fs(fs), _path(path) {}

bool PendingWelcomeTable::isWelcomed(const uint8_t pub_key[BBS_PUBKEY_LEN]) const {
  IFile* f = _fs.open(_path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return false;
  }
  uint8_t buf[BBS_PUBKEY_LEN];
  bool found = false;
  while (f->read(buf, BBS_PUBKEY_LEN) == BBS_PUBKEY_LEN) {
    if (memcmp(buf, pub_key, BBS_PUBKEY_LEN) == 0) {
      found = true;
      break;
    }
  }
  f->close();
  return found;
}

bool PendingWelcomeTable::markWelcomed(const uint8_t pub_key[BBS_PUBKEY_LEN]) {
  IFile* f = _fs.open(_path, 'a');
  if (!f || !f->valid()) {
    if (f) f->close();
    return false;
  }
  bool ok = (f->write(pub_key, BBS_PUBKEY_LEN) == BBS_PUBKEY_LEN);
  f->close();
  return ok;
}

} // namespace bbs
