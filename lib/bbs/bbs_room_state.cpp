#include "bbs_room_state.h"

#include <cstdio>
#include <cstring>

namespace bbs {

RoomState::RoomState(IFileSystem& fs, const char* path) : _fs(fs), _path(path) {}

bool RoomState::isClosed(uint8_t room_id) const {
  if (room_id >= BBS_MAX_ROOMS) return false;

  IFile* f = _fs.open(_path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return false;  // file assente: tutte le stanze aperte
  }
  uint8_t state[BBS_MAX_ROOMS];
  memset(state, 0, sizeof(state));
  f->read(state, sizeof(state));  // se piu' corto del previsto, il resto resta 0 (aperto)
  f->close();
  return state[room_id] != 0;
}

bool RoomState::setClosed(uint8_t room_id, bool closed) {
  if (room_id >= BBS_MAX_ROOMS) return false;

  uint8_t state[BBS_MAX_ROOMS];
  memset(state, 0, sizeof(state));
  IFile* f = _fs.open(_path, 'r');
  if (f && f->valid()) {
    f->read(state, sizeof(state));
  }
  if (f) f->close();

  state[room_id] = closed ? 1 : 0;

  char tmp_path[32];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", _path);
  IFile* dst = _fs.open(tmp_path, 'w');
  if (!dst || !dst->valid()) {
    if (dst) dst->close();
    return false;
  }
  bool ok = (dst->write(state, sizeof(state)) == sizeof(state));
  dst->close();
  if (!ok) {
    _fs.remove(tmp_path);
    return false;
  }

  _fs.remove(_path);
  return _fs.rename(tmp_path, _path);
}

} // namespace bbs
