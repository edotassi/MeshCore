#include "bbs_user_store.h"

#include <cstring>

#include "bbs_binary_io.h"
#include "bbs_room_registry.h"

namespace bbs {

namespace {

bool readU32(IFile& f, uint32_t& out) {
  uint8_t buf[4];
  if (f.read(buf, 4) != 4) return false;
  out = getU32(buf);
  return true;
}

size_t writeU32(IFile& f, uint32_t v) {
  uint8_t buf[4];
  putU32(buf, v);
  return f.write(buf, 4);
}

} // namespace

UserStore::UserStore(IFileSystem& fs, const char* path) : _fs(fs), _path(path) {}

bool UserStore::readFieldsFromFile(IFile& f, UserRecord& rec) const {
  bool ok = (f.read(rec.pub_key, BBS_PUBKEY_LEN) == BBS_PUBKEY_LEN);
  ok = ok && (f.read((uint8_t*)rec.nickname, BBS_NICK_LEN) == BBS_NICK_LEN);
  ok = ok && (f.read(&rec.role, 1) == 1);
  ok = ok && (f.read(&rec.flags, 1) == 1);
  ok = ok && readU32(f, rec.created_ts);
  ok = ok && readU32(f, rec.last_login_ts);
  for (int i = 0; i < BBS_MAX_ROOMS && ok; i++) {
    ok = readU32(f, rec.last_read[i]);
  }
  ok = ok && readU32(f, rec.last_mail_read_seq);
  ok = ok && (f.read(&rec.subscribed_rooms, 1) == 1);
  ok = ok && (f.read(rec.reserved, sizeof(rec.reserved)) == sizeof(rec.reserved));
  return ok;
}

bool UserStore::writeFieldsToFile(IFile& f, const UserRecord& rec) const {
  bool ok = (f.write(rec.pub_key, BBS_PUBKEY_LEN) == BBS_PUBKEY_LEN);
  ok = ok && (f.write((const uint8_t*)rec.nickname, BBS_NICK_LEN) == BBS_NICK_LEN);
  ok = ok && (f.write(&rec.role, 1) == 1);
  ok = ok && (f.write(&rec.flags, 1) == 1);
  ok = ok && (writeU32(f, rec.created_ts) == 4);
  ok = ok && (writeU32(f, rec.last_login_ts) == 4);
  for (int i = 0; i < BBS_MAX_ROOMS && ok; i++) {
    ok = (writeU32(f, rec.last_read[i]) == 4);
  }
  ok = ok && (writeU32(f, rec.last_mail_read_seq) == 4);
  ok = ok && (f.write(&rec.subscribed_rooms, 1) == 1);
  ok = ok && (f.write(rec.reserved, sizeof(rec.reserved)) == sizeof(rec.reserved));
  return ok;
}

UserId UserStore::count() const {
  IFile* f = _fs.open(_path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return 0;
  }
  uint32_t sz = f->size();
  f->close();
  return sz / BBS_USER_RECORD_SIZE;
}

UserId UserStore::findByPubkey(const uint8_t pub_key[BBS_PUBKEY_LEN]) const {
  IFile* f = _fs.open(_path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return kInvalidUserId;
  }
  UserId id = 0;
  UserRecord rec;
  while (readFieldsFromFile(*f, rec)) {
    if (memcmp(rec.pub_key, pub_key, BBS_PUBKEY_LEN) == 0) {
      f->close();
      return id;
    }
    id++;
  }
  f->close();
  return kInvalidUserId;
}

UserId UserStore::findByNickname(const char* nickname) const {
  IFile* f = _fs.open(_path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return kInvalidUserId;
  }
  UserId id = 0;
  UserRecord rec;
  while (readFieldsFromFile(*f, rec)) {
    if (strcmp(rec.nickname, nickname) == 0) {
      f->close();
      return id;
    }
    id++;
  }
  f->close();
  return kInvalidUserId;
}

bool UserStore::readRecordAt(UserId id, UserRecord& out) const {
  IFile* f = _fs.open(_path, 'r');
  if (!f || !f->valid()) {
    if (f) f->close();
    return false;
  }
  bool ok = f->seek((uint32_t)id * BBS_USER_RECORD_SIZE) && readFieldsFromFile(*f, out);
  f->close();
  return ok;
}

bool UserStore::isValidNickname(const char* nickname) {
  if (!nickname) return false;
  size_t len = 0;
  for (; nickname[len] != 0; len++) {
    if (len >= BBS_NICK_MAX_CHARS) return false;
    char c = nickname[len];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return len >= 1;
}

RegisterResult UserStore::registerUser(const uint8_t pub_key[BBS_PUBKEY_LEN], const char* nickname,
                                        uint32_t now_ts, UserId& out_id) {
  if (!isValidNickname(nickname)) return RegisterResult::INVALID_NICKNAME;
  if (findByPubkey(pub_key) != kInvalidUserId) return RegisterResult::ALREADY_EXISTS;

  UserId n = count();
  if (n >= BBS_MAX_USERS) return RegisterResult::STORE_FULL;

  UserRecord rec;
  memset(&rec, 0, sizeof(rec));
  memcpy(rec.pub_key, pub_key, BBS_PUBKEY_LEN);
  size_t i = 0;
  for (; nickname[i] != 0 && i < BBS_NICK_LEN - 1; i++) rec.nickname[i] = nickname[i];
  for (; i < BBS_NICK_LEN; i++) rec.nickname[i] = 0;
  rec.role = (n == 0) ? ROLE_ADMIN : ROLE_USER;  // il primo utente registrato e' admin (bootstrap)
  rec.flags = 0;
  rec.created_ts = now_ts;
  rec.last_login_ts = now_ts;
  rec.subscribed_rooms = kDefaultSubscribedMask;

  IFile* f = _fs.open(_path, 'a');
  if (!f || !f->valid()) {
    if (f) f->close();
    return RegisterResult::IO_ERROR;
  }
  bool ok = writeFieldsToFile(*f, rec);
  f->close();
  if (!ok) return RegisterResult::IO_ERROR;

  out_id = n;
  return RegisterResult::OK;
}

bool UserStore::rewriteApplying(UserId id, void (*mutate)(UserRecord&, uint32_t, uint32_t), uint32_t arg1,
                                 uint32_t arg2) {
  UserId n = count();
  if (id >= n) return false;

  IFile* src = _fs.open(_path, 'r');
  if (!src || !src->valid()) {
    if (src) src->close();
    return false;
  }

  const char* tmp_path = "/bbs/users.dat.tmp";
  IFile* dst = _fs.open(tmp_path, 'w');
  if (!dst || !dst->valid()) {
    src->close();
    if (dst) dst->close();
    return false;
  }

  bool ok = true;
  for (UserId i = 0; i < n && ok; i++) {
    UserRecord rec;
    ok = readFieldsFromFile(*src, rec);
    if (ok && i == id) mutate(rec, arg1, arg2);
    ok = ok && writeFieldsToFile(*dst, rec);
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

bool UserStore::touchLogin(UserId id, uint32_t now_ts) {
  return rewriteApplying(id, [](UserRecord& r, uint32_t now, uint32_t) { r.last_login_ts = now; }, now_ts, 0);
}

uint32_t UserStore::getLastRead(UserId id, uint8_t room_id) const {
  if (room_id >= BBS_MAX_ROOMS) return 0;
  UserRecord rec;
  if (!readRecordAt(id, rec)) return 0;
  return rec.last_read[room_id];
}

bool UserStore::setLastRead(UserId id, uint8_t room_id, uint32_t timestamp) {
  if (room_id >= BBS_MAX_ROOMS) return false;
  return rewriteApplying(
      id, [](UserRecord& r, uint32_t room, uint32_t ts) { r.last_read[room] = ts; }, room_id, timestamp);
}

uint32_t UserStore::getLastMailRead(UserId id) const {
  UserRecord rec;
  if (!readRecordAt(id, rec)) return 0;
  return rec.last_mail_read_seq;
}

bool UserStore::setLastMailRead(UserId id, uint32_t timestamp) {
  return rewriteApplying(
      id, [](UserRecord& r, uint32_t ts, uint32_t) { r.last_mail_read_seq = ts; }, timestamp, 0);
}

bool UserStore::isSubscribed(UserId id, uint8_t room_id) const {
  if (room_id >= BBS_MAX_ROOMS) return false;
  UserRecord rec;
  if (!readRecordAt(id, rec)) return false;
  return (rec.subscribed_rooms & (1u << room_id)) != 0;
}

bool UserStore::setSubscribed(UserId id, uint8_t room_id, bool subscribed) {
  if (room_id >= BBS_MAX_ROOMS) return false;
  uint32_t bit = (1u << room_id);
  return rewriteApplying(
      id,
      [](UserRecord& r, uint32_t bit_arg, uint32_t subscribed_arg) {
        if (subscribed_arg) r.subscribed_rooms |= (uint8_t)bit_arg;
        else r.subscribed_rooms &= (uint8_t)~bit_arg;
      },
      bit, subscribed ? 1u : 0u);
}

bool UserStore::clearRoomForAllUsers(uint8_t room_id) {
  if (room_id >= BBS_MAX_ROOMS) return false;
  UserId n = count();
  if (n == 0) return true;

  IFile* src = _fs.open(_path, 'r');
  if (!src || !src->valid()) {
    if (src) src->close();
    return false;
  }

  const char* tmp_path = "/bbs/users.dat.tmp";
  IFile* dst = _fs.open(tmp_path, 'w');
  if (!dst || !dst->valid()) {
    src->close();
    if (dst) dst->close();
    return false;
  }

  uint8_t bit = (uint8_t)(1u << room_id);
  bool ok = true;
  for (UserId i = 0; i < n && ok; i++) {
    UserRecord rec;
    ok = readFieldsFromFile(*src, rec);
    if (ok) {
      rec.subscribed_rooms &= (uint8_t)~bit;
      rec.last_read[room_id] = 0;
    }
    ok = ok && writeFieldsToFile(*dst, rec);
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

bool UserStore::getNickname(UserId id, char* out_buf, size_t out_cap) const {
  if (out_cap < 1) return false;
  UserRecord rec;
  if (!readRecordAt(id, rec)) return false;
  size_t i = 0;
  for (; i < BBS_NICK_LEN && i < out_cap - 1 && rec.nickname[i] != 0; i++) out_buf[i] = rec.nickname[i];
  out_buf[i] = 0;
  return true;
}

bool UserStore::getPubkey(UserId id, uint8_t out_buf[BBS_PUBKEY_LEN]) const {
  UserRecord rec;
  if (!readRecordAt(id, rec)) return false;
  memcpy(out_buf, rec.pub_key, BBS_PUBKEY_LEN);
  return true;
}

UserRole UserStore::getRole(UserId id) const {
  UserRecord rec;
  if (!readRecordAt(id, rec)) return ROLE_USER;
  return (UserRole)rec.role;
}

bool UserStore::setRole(UserId id, UserRole role) {
  return rewriteApplying(
      id, [](UserRecord& r, uint32_t role_arg, uint32_t) { r.role = (uint8_t)role_arg; }, (uint32_t)role, 0);
}

bool UserStore::isBanned(UserId id) const {
  UserRecord rec;
  if (!readRecordAt(id, rec)) return false;
  return (rec.flags & kUserFlagBanned) != 0;
}

bool UserStore::setBanned(UserId id, bool banned) {
  return rewriteApplying(
      id,
      [](UserRecord& r, uint32_t flag, uint32_t on) {
        if (on) r.flags |= (uint8_t)flag;
        else r.flags &= (uint8_t)~flag;
      },
      kUserFlagBanned, banned ? 1u : 0u);
}

bool UserStore::isMuted(UserId id) const {
  UserRecord rec;
  if (!readRecordAt(id, rec)) return false;
  return (rec.flags & kUserFlagMuted) != 0;
}

bool UserStore::setMuted(UserId id, bool muted) {
  return rewriteApplying(
      id,
      [](UserRecord& r, uint32_t flag, uint32_t on) {
        if (on) r.flags |= (uint8_t)flag;
        else r.flags &= (uint8_t)~flag;
      },
      kUserFlagMuted, muted ? 1u : 0u);
}

} // namespace bbs
