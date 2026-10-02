#include "bbs_room_registry.h"

#include <cstdio>
#include <cstring>

namespace bbs {

namespace {

constexpr RoomInfo kDefaultRooms[] = {
    {0, "Generale"},
    {1, "Annunci"},
    {2, "Tecnico"},
};
constexpr size_t kNumDefaultRooms = sizeof(kDefaultRooms) / sizeof(kDefaultRooms[0]);

void copyName(char* dst, const char* src) {
  size_t i = 0;
  for (; src[i] != 0 && i < BBS_ROOM_NAME_LEN - 1; i++) dst[i] = src[i];
  for (; i < BBS_ROOM_NAME_LEN; i++) dst[i] = 0;
}

} // namespace

RoomRegistry::RoomRegistry(IFileSystem& fs, const char* path) : _fs(fs), _path(path) {
  load();
}

void RoomRegistry::load() {
  IFile* f = _fs.open(_path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    _count = kNumDefaultRooms;
    for (size_t i = 0; i < _count; i++) _rooms[i] = kDefaultRooms[i];
    return;
  }

  uint8_t count_byte = 0;
  bool ok = (f->read(&count_byte, 1) == 1) && count_byte <= BBS_MAX_ROOMS;
  size_t n = 0;
  for (; ok && n < count_byte; n++) {
    uint8_t id;
    char name[BBS_ROOM_NAME_LEN];
    ok = (f->read(&id, 1) == 1) && (f->read((uint8_t*)name, BBS_ROOM_NAME_LEN) == BBS_ROOM_NAME_LEN);
    if (!ok) break;
    _rooms[n].id = id;
    memcpy(_rooms[n].name, name, BBS_ROOM_NAME_LEN);
    _rooms[n].name[BBS_ROOM_NAME_LEN - 1] = 0;
  }
  f->close();

  if (!ok) {
    // File presente ma illeggibile/troncato: meglio i default noti che uno
    // stato parziale.
    _count = kNumDefaultRooms;
    for (size_t i = 0; i < _count; i++) _rooms[i] = kDefaultRooms[i];
    return;
  }
  _count = n;
}

bool RoomRegistry::persist(const RoomInfo* rooms, size_t count) const {
  char tmp_path[40];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", _path);
  IFile* dst = _fs.open(tmp_path, 'w');
  if (!dst || !dst->valid()) {
    if (dst) dst->close();
    return false;
  }

  uint8_t count_byte = (uint8_t)count;
  bool ok = (dst->write(&count_byte, 1) == 1);
  for (size_t i = 0; ok && i < count; i++) {
    ok = ok && (dst->write(&rooms[i].id, 1) == 1);
    ok = ok && (dst->write((const uint8_t*)rooms[i].name, BBS_ROOM_NAME_LEN) == BBS_ROOM_NAME_LEN);
  }
  dst->close();
  if (!ok) {
    _fs.remove(tmp_path);
    return false;
  }

  _fs.remove(_path);
  return _fs.rename(tmp_path, _path);
}

const RoomInfo* RoomRegistry::findRoom(uint8_t id) const {
  for (size_t i = 0; i < _count; i++) {
    if (_rooms[i].id == id) return &_rooms[i];
  }
  return nullptr;
}

bool RoomRegistry::isValidName(const char* name) {
  if (!name) return false;
  size_t len = 0;
  for (; name[len] != 0; len++) {
    if (len >= BBS_ROOM_NAME_MAX_CHARS) return false;
    char c = name[len];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return len >= 1;
}

AddRoomResult RoomRegistry::addRoom(const char* name, uint8_t& out_id) {
  if (!isValidName(name)) return AddRoomResult::INVALID_NAME;
  if (_count >= BBS_MAX_ROOMS) return AddRoomResult::FULL;
  for (size_t i = 0; i < _count; i++) {
    if (strcmp(_rooms[i].name, name) == 0) return AddRoomResult::DUPLICATE_NAME;
  }

  uint8_t id = 0;
  for (; id < BBS_MAX_ROOMS; id++) {
    if (findRoom(id) == nullptr) break;
  }

  RoomInfo candidate[BBS_MAX_ROOMS];
  memcpy(candidate, _rooms, sizeof(RoomInfo) * _count);
  candidate[_count].id = id;
  copyName(candidate[_count].name, name);
  size_t new_count = _count + 1;

  if (!persist(candidate, new_count)) return AddRoomResult::IO_ERROR;

  memcpy(_rooms, candidate, sizeof(RoomInfo) * new_count);
  _count = new_count;
  out_id = id;
  return AddRoomResult::OK;
}

RemoveRoomResult RoomRegistry::removeRoom(uint8_t id) {
  size_t idx = _count;
  for (size_t i = 0; i < _count; i++) {
    if (_rooms[i].id == id) {
      idx = i;
      break;
    }
  }
  if (idx == _count) return RemoveRoomResult::NOT_FOUND;

  RoomInfo candidate[BBS_MAX_ROOMS];
  size_t n = 0;
  for (size_t i = 0; i < _count; i++) {
    if (i == idx) continue;
    candidate[n++] = _rooms[i];
  }

  if (!persist(candidate, n)) return RemoveRoomResult::IO_ERROR;

  memcpy(_rooms, candidate, sizeof(RoomInfo) * n);
  _count = n;
  return RemoveRoomResult::OK;
}

} // namespace bbs
