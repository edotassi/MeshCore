#include "bbs_command_parser.h"

#include <cstdio>
#include <cstring>

#include "bbs_room_registry.h"
#include "bbs_strings_it.h"
#include "bbs_tokenize.h"

namespace bbs {

namespace {

size_t copyOut(const char* text, char* out, size_t out_cap) {
  size_t len = 0;
  while (text[len] != 0 && len + 1 < out_cap) {
    out[len] = text[len];
    len++;
  }
  out[len] = 0;
  return len;
}

size_t copyOut2(const char* a, const char* b, char* out, size_t out_cap) {
  size_t len = 0;
  for (const char* p = a; *p != 0 && len + 1 < out_cap; p++) out[len++] = *p;
  for (const char* p = b; *p != 0 && len + 1 < out_cap; p++) out[len++] = *p;
  out[len] = 0;
  return len;
}

size_t copyOut3(const char* a, const char* b, const char* c, char* out, size_t out_cap) {
  size_t len = 0;
  for (const char* p = a; *p != 0 && len + 1 < out_cap; p++) out[len++] = *p;
  for (const char* p = b; *p != 0 && len + 1 < out_cap; p++) out[len++] = *p;
  for (const char* p = c; *p != 0 && len + 1 < out_cap; p++) out[len++] = *p;
  out[len] = 0;
  return len;
}

size_t handleRooms(CommandContext& ctx, UserId uid, char* out, size_t out_cap) {
  size_t len = 0;
  size_t n_rooms = ctx.room_registry.count();
  for (size_t i = 0; i < n_rooms && len + 1 < out_cap; i++) {
    const RoomInfo& room = ctx.room_registry.at(i);
    if (i > 0 && len + 1 < out_cap) out[len++] = '\n';
    char id_buf[4];
    int n = snprintf(id_buf, sizeof(id_buf), "%u:", (unsigned)room.id);
    for (int j = 0; j < n && len + 1 < out_cap; j++) out[len++] = id_buf[j];
    for (const char* p = room.name; *p != 0 && len + 1 < out_cap; p++) out[len++] = *p;

    // "(da leggere/totale)": il totale conta tutti i post non cancellati
    // della stanza (0 come "ultimo letto" = nessun filtro); i da leggere
    // usano il puntatore "ultimo letto" di QUESTO utente per quella stanza.
    uint32_t total = ctx.posts.countUnread(room.id, 0);
    uint32_t unread = ctx.posts.countUnread(room.id, ctx.users.getLastRead(uid, room.id));
    char count_buf[24];
    int cn = snprintf(count_buf, sizeof(count_buf), "(%u/%u)", (unsigned)unread, (unsigned)total);
    for (int j = 0; j < cn && len + 1 < out_cap; j++) out[len++] = count_buf[j];
  }
  out[len] = 0;
  return len;
}

// Pubblica 'text' nella stanza corrente dell'utente (handleNew/E, e il
// testo libero senza comando riconosciuto, vedi processCommand).
size_t postToCurrentRoom(CommandContext& ctx, const char* text, UserId uid, char* out, size_t out_cap) {
  uint8_t room_id = ctx.users.getCurrentRoom(uid);
  if (room_id == kNoRoom) return copyOut(strings::kNoRoomSelected, out, out_cap);
  const RoomInfo* room = ctx.room_registry.findRoom(room_id);
  if (!room) return copyOut(strings::kInvalidRoom, out, out_cap);
  if (text[0] == 0) return copyOut(strings::kPostEmptyText, out, out_cap);

  if (ctx.users.isMuted(uid)) return copyOut(strings::kMutedCannotPost, out, out_cap);
  if (ctx.rooms.isClosed(room_id)) return copyOut(strings::kRoomClosed, out, out_cap);
  if (!ctx.sessions.allowMessage(uid, ctx.now_ts)) return copyOut(strings::kRateLimited, out, out_cap);

  if (!ctx.posts.appendPost(room_id, uid, ctx.now_ts, text)) {
    return copyOut(strings::kPostEmptyText, out, out_cap);
  }
  ctx.posts.enforceRetention(room_id, BBS_MAX_POSTS_PER_ROOM);
  return copyOut2(strings::kPostOkPrefix, room->name, out, out_cap);
}

size_t handleNew(CommandContext& ctx, UserId uid, char* out, size_t out_cap) {
  uint8_t room_id = ctx.users.getCurrentRoom(uid);
  if (room_id == kNoRoom) return copyOut(strings::kNoRoomSelected, out, out_cap);
  const RoomInfo* room = ctx.room_registry.findRoom(room_id);
  if (!room) return copyOut(strings::kInvalidRoom, out, out_cap);

  uint32_t after_ts = ctx.users.getLastRead(uid, room_id);

  PostRecord post;
  if (!ctx.posts.findNextUnread(room_id, after_ts, post)) {
    return copyOut2(strings::kNoNewPostsPrefix, room->name, out, out_cap);
  }

  ctx.users.setLastRead(uid, room_id, post.timestamp);

  char author[BBS_NICK_LEN];
  if (!ctx.users.getNickname(post.user_id, author, sizeof(author))) {
    strncpy(author, "???", sizeof(author));
    author[sizeof(author) - 1] = 0;
  }
  return copyOut3(author, ": ", post.text, out, out_cap);
}

size_t handleMail(CommandContext& ctx, const char* args, UserId uid, char* out, size_t out_cap) {
  if (args[0] == 0) {
    // "M" senza argomenti: leggi la prossima mail non letta.
    uint32_t after_ts = ctx.users.getLastMailRead(uid);
    MailRecord mail;
    if (!ctx.mail.findNextUnread(uid, after_ts, mail)) {
      return copyOut(strings::kMailNoNew, out, out_cap);
    }
    ctx.users.setLastMailRead(uid, mail.timestamp);

    char sender[BBS_NICK_LEN];
    if (!ctx.users.getNickname(mail.sender_id, sender, sizeof(sender))) {
      strncpy(sender, "???", sizeof(sender));
      sender[sizeof(sender) - 1] = 0;
    }
    return copyOut3(sender, ": ", mail.text, out, out_cap);
  }

  // "M <nickname> <testo>": invia.
  char nick[BBS_NICK_LEN];
  const char* text = extractToken(args, nick, sizeof(nick));
  UserId recipient = ctx.users.findByNickname(nick);
  if (recipient == kInvalidUserId) {
    return copyOut(strings::kMailUnknownRecipient, out, out_cap);
  }
  if (text[0] == 0) {
    return copyOut(strings::kMailEmptyText, out, out_cap);
  }
  if (ctx.users.isMuted(uid)) return copyOut(strings::kMutedCannotPost, out, out_cap);
  if (!ctx.sessions.allowMessage(uid, ctx.now_ts)) return copyOut(strings::kRateLimited, out, out_cap);

  if (!ctx.mail.send(uid, recipient, ctx.now_ts, text)) {
    return copyOut(strings::kMailEmptyText, out, out_cap);
  }
  return copyOut2(strings::kMailSentPrefix, nick, out, out_cap);
}

// Un id di stanza obbligatorio (non un prefisso opzionale come in E/N):
// "S 2" oppure "U 2". Nessun testo libero atteso dopo.
bool parseRoomIdArg(const RoomRegistry& registry, const char* args, uint8_t& room_id) {
  if (args[0] < '0' || args[0] > '9') return false;
  const char* p = args;
  uint32_t v = 0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (uint32_t)(*p - '0');
    p++;
  }
  if (*p != 0 && *p != ' ') return false;
  if (v >= BBS_MAX_ROOMS || registry.findRoom((uint8_t)v) == nullptr) return false;
  room_id = (uint8_t)v;
  return true;
}

size_t handleJoinRoom(CommandContext& ctx, const char* args, UserId uid, char* out, size_t out_cap) {
  uint8_t room_id;
  if (!parseRoomIdArg(ctx.room_registry, args, room_id)) {
    return copyOut(strings::kInvalidRoom, out, out_cap);
  }
  ctx.users.setCurrentRoom(uid, room_id);
  const RoomInfo* room = ctx.room_registry.findRoom(room_id);

  size_t len = copyOut2(strings::kJoinedRoomPrefix, room->name, out, out_cap);

  PostRecord pinned;
  if (ctx.posts.findPinned(room_id, pinned)) {
    char author[BBS_NICK_LEN];
    if (!ctx.users.getNickname(pinned.user_id, author, sizeof(author))) {
      strncpy(author, "???", sizeof(author));
      author[sizeof(author) - 1] = 0;
    }
    if (len + 1 < out_cap) out[len++] = '\n';
    for (const char* p = strings::kPinnedInline; *p != 0 && len + 1 < out_cap; p++) out[len++] = *p;
    for (const char* p = author; *p != 0 && len + 1 < out_cap; p++) out[len++] = *p;
    for (const char* p = ": "; *p != 0 && len + 1 < out_cap; p++) out[len++] = *p;
    for (const char* p = pinned.text; *p != 0 && len + 1 < out_cap; p++) out[len++] = *p;
  }
  out[len] = 0;
  return len;
}

size_t handleLeaveRoom(CommandContext& ctx, UserId uid, char* out, size_t out_cap) {
  uint8_t room_id = ctx.users.getCurrentRoom(uid);
  if (room_id == kNoRoom) return copyOut(strings::kNotInAnyRoom, out, out_cap);
  const RoomInfo* room = ctx.room_registry.findRoom(room_id);
  ctx.users.setCurrentRoom(uid, kNoRoom);
  return copyOut2(strings::kLeftRoomPrefix, room ? room->name : "?", out, out_cap);
}

// --- Fase 3: ruoli, moderazione, anti-abuso ---

bool hasAtLeastRole(CommandContext& ctx, UserId uid, UserRole min_role) {
  return ctx.users.getRole(uid) >= min_role;
}

size_t handleBanCmd(CommandContext& ctx, const char* args, UserId actor, bool ban, char* out, size_t out_cap) {
  if (!hasAtLeastRole(ctx, actor, ROLE_MODERATOR)) return copyOut(strings::kPermissionDenied, out, out_cap);
  char nick[BBS_NICK_LEN];
  extractToken(args, nick, sizeof(nick));
  UserId target = ctx.users.findByNickname(nick);
  if (target == kInvalidUserId) return copyOut(strings::kTargetNotFound, out, out_cap);
  ctx.users.setBanned(target, ban);
  ctx.modlog.record(actor, ban ? ModAction::BAN : ModAction::UNBAN, target, 0xFF, ctx.now_ts);
  return copyOut2(ban ? strings::kBanOkPrefix : strings::kUnbanOkPrefix, nick, out, out_cap);
}

size_t handleMuteCmd(CommandContext& ctx, const char* args, UserId actor, bool mute, char* out, size_t out_cap) {
  if (!hasAtLeastRole(ctx, actor, ROLE_MODERATOR)) return copyOut(strings::kPermissionDenied, out, out_cap);
  char nick[BBS_NICK_LEN];
  extractToken(args, nick, sizeof(nick));
  UserId target = ctx.users.findByNickname(nick);
  if (target == kInvalidUserId) return copyOut(strings::kTargetNotFound, out, out_cap);
  ctx.users.setMuted(target, mute);
  ctx.modlog.record(actor, mute ? ModAction::MUTE : ModAction::UNMUTE, target, 0xFF, ctx.now_ts);
  return copyOut2(mute ? strings::kMuteOkPrefix : strings::kUnmuteOkPrefix, nick, out, out_cap);
}

size_t handleDelPostCmd(CommandContext& ctx, const char* args, UserId actor, char* out, size_t out_cap) {
  if (!hasAtLeastRole(ctx, actor, ROLE_MODERATOR)) return copyOut(strings::kPermissionDenied, out, out_cap);
  uint8_t room_id;
  if (!parseRoomIdArg(ctx.room_registry, args, room_id)) return copyOut(strings::kInvalidRoom, out, out_cap);
  if (!ctx.posts.deleteLastPost(room_id)) return copyOut(strings::kDelPostNothingToDelete, out, out_cap);
  ctx.modlog.record(actor, ModAction::DELPOST, kInvalidUserId, room_id, ctx.now_ts);
  return copyOut2(strings::kDelPostOkPrefix, ctx.room_registry.findRoom(room_id)->name, out, out_cap);
}

size_t handleRoomOpenClose(CommandContext& ctx, const char* args, UserId actor, bool close, char* out,
                            size_t out_cap) {
  if (!hasAtLeastRole(ctx, actor, ROLE_MODERATOR)) return copyOut(strings::kPermissionDenied, out, out_cap);
  uint8_t room_id;
  if (!parseRoomIdArg(ctx.room_registry, args, room_id)) return copyOut(strings::kInvalidRoom, out, out_cap);
  ctx.rooms.setClosed(room_id, close);
  ctx.modlog.record(actor, close ? ModAction::CLOSE_ROOM : ModAction::OPEN_ROOM, kInvalidUserId, room_id,
                     ctx.now_ts);
  return copyOut2(close ? strings::kCloseOkPrefix : strings::kOpenOkPrefix, ctx.room_registry.findRoom(room_id)->name,
                   out, out_cap);
}

size_t handleRoomAddCmd(CommandContext& ctx, const char* args, UserId actor, char* out, size_t out_cap) {
  if (!hasAtLeastRole(ctx, actor, ROLE_ADMIN)) return copyOut(strings::kPermissionDenied, out, out_cap);
  if (args[0] == 0) return copyOut(strings::kRoomUsage, out, out_cap);

  uint8_t id;
  AddRoomResult r = ctx.room_registry.addRoom(args, id);
  switch (r) {
    case AddRoomResult::OK:
      ctx.modlog.record(actor, ModAction::ADD_ROOM, kInvalidUserId, id, ctx.now_ts);
      return copyOut2(strings::kRoomAddOkPrefix, args, out, out_cap);
    case AddRoomResult::FULL:
      return copyOut(strings::kRoomsFull, out, out_cap);
    case AddRoomResult::DUPLICATE_NAME:
      return copyOut(strings::kRoomNameDuplicate, out, out_cap);
    case AddRoomResult::IO_ERROR:
      return copyOut(strings::kRoomAddFailed, out, out_cap);
    case AddRoomResult::INVALID_NAME:
    default:
      return copyOut(strings::kRoomNameInvalid, out, out_cap);
  }
}

size_t handleRoomDelCmd(CommandContext& ctx, const char* args, UserId actor, char* out, size_t out_cap) {
  if (!hasAtLeastRole(ctx, actor, ROLE_ADMIN)) return copyOut(strings::kPermissionDenied, out, out_cap);
  uint8_t room_id;
  if (!parseRoomIdArg(ctx.room_registry, args, room_id)) return copyOut(strings::kInvalidRoom, out, out_cap);

  char name_buf[BBS_ROOM_NAME_LEN];
  strncpy(name_buf, ctx.room_registry.findRoom(room_id)->name, sizeof(name_buf));

  if (ctx.room_registry.removeRoom(room_id) != RemoveRoomResult::OK) {
    return copyOut(strings::kRoomDelFailed, out, out_cap);
  }
  // L'id cancellato verra' riassegnato a una stanza futura: non deve
  // eredire post, iscrizioni/last_read o lo stato aperta/chiusa di questa.
  ctx.posts.purgeRoom(room_id);
  ctx.users.clearRoomForAllUsers(room_id);
  ctx.rooms.setClosed(room_id, false);

  ctx.modlog.record(actor, ModAction::DEL_ROOM, kInvalidUserId, room_id, ctx.now_ts);
  return copyOut2(strings::kRoomDelOkPrefix, name_buf, out, out_cap);
}

size_t handlePinCmd(CommandContext& ctx, const char* args, UserId actor, char* out, size_t out_cap) {
  if (!hasAtLeastRole(ctx, actor, ROLE_MODERATOR)) return copyOut(strings::kPermissionDenied, out, out_cap);
  uint8_t room_id;
  if (!parseRoomIdArg(ctx.room_registry, args, room_id)) return copyOut(strings::kInvalidRoom, out, out_cap);
  if (!ctx.posts.pinLastPost(room_id)) return copyOut(strings::kPinNothingToPin, out, out_cap);
  ctx.modlog.record(actor, ModAction::PIN_POST, kInvalidUserId, room_id, ctx.now_ts);
  return copyOut2(strings::kPinOkPrefix, ctx.room_registry.findRoom(room_id)->name, out, out_cap);
}

size_t handleUnpinCmd(CommandContext& ctx, const char* args, UserId actor, char* out, size_t out_cap) {
  if (!hasAtLeastRole(ctx, actor, ROLE_MODERATOR)) return copyOut(strings::kPermissionDenied, out, out_cap);
  uint8_t room_id;
  if (!parseRoomIdArg(ctx.room_registry, args, room_id)) return copyOut(strings::kInvalidRoom, out, out_cap);
  if (!ctx.posts.unpinRoom(room_id)) return copyOut(strings::kUnpinNothingToUnpin, out, out_cap);
  ctx.modlog.record(actor, ModAction::UNPIN_POST, kInvalidUserId, room_id, ctx.now_ts);
  return copyOut2(strings::kUnpinOkPrefix, ctx.room_registry.findRoom(room_id)->name, out, out_cap);
}

size_t handlePinnedCmd(CommandContext& ctx, UserId uid, char* out, size_t out_cap) {
  uint8_t room_id = ctx.users.getCurrentRoom(uid);
  if (room_id == kNoRoom) return copyOut(strings::kNoRoomSelected, out, out_cap);
  const RoomInfo* room = ctx.room_registry.findRoom(room_id);
  if (!room) return copyOut(strings::kInvalidRoom, out, out_cap);

  PostRecord post;
  if (!ctx.posts.findPinned(room_id, post)) return copyOut2(strings::kNoPinnedPrefix, room->name, out, out_cap);

  char author[BBS_NICK_LEN];
  if (!ctx.users.getNickname(post.user_id, author, sizeof(author))) {
    strncpy(author, "???", sizeof(author));
    author[sizeof(author) - 1] = 0;
  }
  return copyOut3(author, ": ", post.text, out, out_cap);
}

size_t handleSetRoleCmd(CommandContext& ctx, const char* args, UserId actor, UserRole role, char* out,
                         size_t out_cap) {
  if (!hasAtLeastRole(ctx, actor, ROLE_ADMIN)) return copyOut(strings::kPermissionDenied, out, out_cap);
  char nick[BBS_NICK_LEN];
  extractToken(args, nick, sizeof(nick));
  UserId target = ctx.users.findByNickname(nick);
  if (target == kInvalidUserId) return copyOut(strings::kTargetNotFound, out, out_cap);
  ctx.users.setRole(target, role);
  ModAction action = (role == ROLE_ADMIN)        ? ModAction::SET_ADMIN
                      : (role == ROLE_MODERATOR) ? ModAction::SET_MODERATOR
                                                  : ModAction::SET_USER;
  ctx.modlog.record(actor, action, target, 0xFF, ctx.now_ts);
  const char* prefix = (role == ROLE_ADMIN)        ? strings::kSetAdminOkPrefix
                        : (role == ROLE_MODERATOR) ? strings::kSetModOkPrefix
                                                    : strings::kSetUserOkPrefix;
  return copyOut2(prefix, nick, out, out_cap);
}

size_t handleModLogCmd(CommandContext& ctx, UserId actor, char* out, size_t out_cap) {
  if (!hasAtLeastRole(ctx, actor, ROLE_MODERATOR)) return copyOut(strings::kPermissionDenied, out, out_cap);
  char buf[64];
  snprintf(buf, sizeof(buf), strings::kModLogCountFmt, (unsigned)ctx.modlog.count());
  return copyOut(buf, out, out_cap);
}

// --- Fase 4: funzioni leggere ---

size_t handleWho(CommandContext& ctx, char* out, size_t out_cap) {
  size_t len = 0;
  size_t n = ctx.sessions.count();
  bool any = false;
  for (size_t i = 0; i < n && len + 1 < out_cap; i++) {
    UserId uid = ctx.sessions.userIdAt(i);
    if (!ctx.sessions.isActive(uid, ctx.now_ts)) continue;
    char nick[BBS_NICK_LEN];
    if (!ctx.users.getNickname(uid, nick, sizeof(nick))) continue;
    if (any && len + 1 < out_cap) out[len++] = '\n';
    for (const char* p = nick; *p != 0 && len + 1 < out_cap; p++) out[len++] = *p;
    any = true;
  }
  if (!any) return copyOut(strings::kWhoNoOne, out, out_cap);
  out[len] = 0;
  return len;
}

size_t handleStats(CommandContext& ctx, char* out, size_t out_cap) {
  uint32_t total_posts = 0;
  for (size_t i = 0; i < ctx.room_registry.count(); i++) {
    total_posts += ctx.posts.countUnread(ctx.room_registry.at(i).id, 0);
  }
  uint32_t total_mail = ctx.mail.totalCount();
  uint32_t total_users = ctx.users.countNonSystem();
  uint32_t active_now = 0;
  size_t n = ctx.sessions.count();
  for (size_t i = 0; i < n; i++) {
    if (ctx.sessions.isActive(ctx.sessions.userIdAt(i), ctx.now_ts)) active_now++;
  }
  char buf[96];
  snprintf(buf, sizeof(buf), strings::kStatsFmt, (unsigned)total_users, (unsigned)active_now,
           (unsigned)total_posts, (unsigned)total_mail);
  return copyOut(buf, out, out_cap);
}

size_t handleSearch(CommandContext& ctx, const char* args, UserId uid, char* out, size_t out_cap) {
  uint8_t room_id = ctx.users.getCurrentRoom(uid);
  if (room_id == kNoRoom) return copyOut(strings::kNoRoomSelected, out, out_cap);
  if (args[0] == 0) return copyOut(strings::kSearchEmptyKeyword, out, out_cap);

  char keyword[BBS_MAX_TEXT_LEN + 1];
  extractToken(args, keyword, sizeof(keyword));

  PostRecord found;
  if (!ctx.posts.searchRecent(room_id, keyword, BBS_SEARCH_MAX_SCAN, found)) {
    return copyOut(strings::kSearchNoMatch, out, out_cap);
  }
  char author[BBS_NICK_LEN];
  if (!ctx.users.getNickname(found.user_id, author, sizeof(author))) {
    strncpy(author, "???", sizeof(author));
    author[sizeof(author) - 1] = 0;
  }
  return copyOut3(author, ": ", found.text, out, out_cap);
}

size_t handleMotdCmd(CommandContext& ctx, const char* args, UserId actor, char* out, size_t out_cap) {
  if (args[0] == 0) {
    // Mostra il MOTD corrente: chiunque puo' chiederlo, e' innocuo.
    char current[BBS_MAX_TEXT_LEN + 1];
    ctx.motd.get(current, sizeof(current));
    if (current[0] == 0) return copyOut(strings::kMotdNotSet, out, out_cap);
    return copyOut(current, out, out_cap);
  }

  if (!hasAtLeastRole(ctx, actor, ROLE_ADMIN)) return copyOut(strings::kPermissionDenied, out, out_cap);

  if (strcmp(args, "CLEAR") == 0) {
    ctx.motd.set("");
    return copyOut(strings::kMotdCleared, out, out_cap);
  }
  ctx.motd.set(args);
  return copyOut(strings::kMotdSetOk, out, out_cap);
}

// --- Riepilogo giornaliero (DIGEST, solo amministratore) ---

bool equalsIgnoreCase(const char* a, const char* b) {
  for (; *a != 0 && *b != 0; a++, b++) {
    char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 'a' + 'A') : *a;
    char cb = (*b >= 'a' && *b <= 'z') ? (char)(*b - 'a' + 'A') : *b;
    if (ca != cb) return false;
  }
  return *a == 0 && *b == 0;
}

// "HH:MM" (1-2 cifre per l'ora, 2 per i minuti).
bool parseHourMinute(const char* s, uint8_t& hour, uint8_t& minute) {
  const char* p = s;
  unsigned h = 0;
  size_t nd = 0;
  while (*p >= '0' && *p <= '9' && nd < 3) {
    h = h * 10 + (unsigned)(*p - '0');
    p++;
    nd++;
  }
  if (nd == 0 || nd > 2 || *p != ':') return false;
  p++;
  if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9' || p[2] != 0) return false;
  unsigned m = (unsigned)(p[0] - '0') * 10 + (unsigned)(p[1] - '0');
  if (h > 23 || m > 59) return false;
  hour = (uint8_t)h;
  minute = (uint8_t)m;
  return true;
}

// "+H", "-H:MM", "H" (segno opzionale), da -12:00 a +14:00. Ritorna minuti.
bool parseTzOffset(const char* s, int16_t& offset_min) {
  const char* p = s;
  int sign = 1;
  if (*p == '+' || *p == '-') {
    if (*p == '-') sign = -1;
    p++;
  }
  unsigned h = 0;
  size_t nd = 0;
  while (*p >= '0' && *p <= '9' && nd < 3) {
    h = h * 10 + (unsigned)(*p - '0');
    p++;
    nd++;
  }
  if (nd == 0 || nd > 2) return false;
  unsigned m = 0;
  if (*p == ':') {
    p++;
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return false;
    m = (unsigned)(p[0] - '0') * 10 + (unsigned)(p[1] - '0');
    p += 2;
  }
  if (*p != 0 || m > 59) return false;
  int total = sign * (int)(h * 60 + m);
  if (total < -720 || total > 840) return false;
  offset_min = (int16_t)total;
  return true;
}

size_t handleDigestCmd(CommandContext& ctx, const char* args, UserId actor, char* out, size_t out_cap) {
  if (!hasAtLeastRole(ctx, actor, ROLE_ADMIN)) return copyOut(strings::kPermissionDenied, out, out_cap);

  char sub[8];
  const char* rest = extractCommand(args, sub, sizeof(sub));
  DigestSettings s = ctx.digest.get();  // persisti-poi-applica: si modifica la copia
  char buf[BBS_MAX_TEXT_LEN + 1];

  if (sub[0] == 0) {
    int tz = s.tz_offset_min;
    unsigned tz_abs = (unsigned)(tz < 0 ? -tz : tz);
    char date[16];
    if (s.last_sent_day == 0) {
      copyOut(strings::kDigestNever, date, sizeof(date));
    } else {
      int y;
      unsigned mo, d;
      civilFromDays((int64_t)s.last_sent_day, y, mo, d);
      snprintf(date, sizeof(date), strings::kDigestDateFmt, y, mo, d);
    }
    snprintf(buf, sizeof(buf), strings::kDigestStatusFmt, s.enabled ? strings::kDigestStatusOn : strings::kDigestStatusOff,
             (unsigned)s.hour, (unsigned)s.minute, tz < 0 ? '-' : '+', tz_abs / 60, tz_abs % 60,
             s.eu_dst ? strings::kDigestDstEu : strings::kDigestDstNone,
             s.usesPublicPsk() ? strings::kDigestChannelPublic : strings::kDigestChannelPrivate, date);
    return copyOut(buf, out, out_cap);
  }

  if (strcmp(sub, "NOW") == 0) {
    ctx.digest.requestNow();
    return copyOut(strings::kDigestNowOk, out, out_cap);
  }

  const char* ok_text = nullptr;
  if (strcmp(sub, "ON") == 0 || strcmp(sub, "OFF") == 0) {
    s.enabled = (sub[1] == 'N');
    ok_text = s.enabled ? strings::kDigestOnOk : strings::kDigestOffOk;
  } else if (strcmp(sub, "TIME") == 0) {
    char tok[8];
    extractToken(rest, tok, sizeof(tok));
    if (!parseHourMinute(tok, s.hour, s.minute)) return copyOut(strings::kDigestBadTime, out, out_cap);
    snprintf(buf, sizeof(buf), strings::kDigestTimeOkFmt, (unsigned)s.hour, (unsigned)s.minute);
    ok_text = buf;
  } else if (strcmp(sub, "TZ") == 0) {
    char tok[12];
    extractToken(rest, tok, sizeof(tok));
    if (!parseTzOffset(tok, s.tz_offset_min)) return copyOut(strings::kDigestBadTz, out, out_cap);
    int tz = s.tz_offset_min;
    unsigned tz_abs = (unsigned)(tz < 0 ? -tz : tz);
    snprintf(buf, sizeof(buf), strings::kDigestTzOkFmt, tz < 0 ? '-' : '+', tz_abs / 60, tz_abs % 60);
    ok_text = buf;
  } else if (strcmp(sub, "DST") == 0) {
    char tok[8];
    extractCommand(rest, tok, sizeof(tok));
    if (strcmp(tok, "ON") == 0) {
      s.eu_dst = true;
    } else if (strcmp(tok, "OFF") == 0) {
      s.eu_dst = false;
    } else {
      return copyOut(strings::kDigestUsage, out, out_cap);
    }
    ok_text = s.eu_dst ? strings::kDigestDstOnOk : strings::kDigestDstOffOk;
  } else if (strcmp(sub, "CHANNEL") == 0) {
    char tok[64];
    extractToken(rest, tok, sizeof(tok));
    if (tok[0] == 0) return copyOut(strings::kDigestUsage, out, out_cap);
    if (equalsIgnoreCase(tok, "PUBLIC")) {
      s.setPublicPsk();
      ok_text = strings::kDigestChannelPublicOk;
    } else {
      uint8_t psk[kDigestPskMaxLen];
      size_t n = base64Decode(tok, psk, sizeof(psk));
      if (n != 16 && n != 32) return copyOut(strings::kDigestBadPsk, out, out_cap);
      memset(s.psk, 0, sizeof(s.psk));
      memcpy(s.psk, psk, n);
      s.psk_len = (uint8_t)n;
      ok_text = strings::kDigestChannelPrivateOk;
    }
  } else {
    return copyOut(strings::kDigestUsage, out, out_cap);
  }

  if (!ctx.digest.save(s)) return copyOut(strings::kDigestSaveFailed, out, out_cap);
  return copyOut(ok_text, out, out_cap);
}

// --- Test bot (BOT, solo amministratore) ---

size_t handleBotCmd(CommandContext& ctx, const char* args, UserId actor, char* out, size_t out_cap) {
  if (!hasAtLeastRole(ctx, actor, ROLE_ADMIN)) return copyOut(strings::kPermissionDenied, out, out_cap);

  char sub[8];
  const char* rest = extractCommand(args, sub, sizeof(sub));

  BotSettings s = ctx.bot.get();  // persisti-poi-applica: si modifica la copia
  char buf[160];
  const char* ok_text = nullptr;

  if (sub[0] == 0) {
    char list[kBotMaxChannels * (kBotNameLen + 2) + 1];
    size_t list_len = 0;
    list[0] = 0;
    for (size_t i = 0; i < s.count; i++) {
      int n = snprintf(list + list_len, sizeof(list) - list_len, "%s%s", i ? ", " : "", s.channels[i].name);
      if (n > 0 && list_len + (size_t)n < sizeof(list)) list_len += (size_t)n;
    }
    snprintf(buf, sizeof(buf), strings::kBotStatusFmt, s.enabled ? strings::kBotStatusOn : strings::kBotStatusOff,
             s.count ? list : strings::kBotNoChannels);
    return copyOut(buf, out, out_cap);
  }

  if (strcmp(sub, "ON") == 0 || strcmp(sub, "OFF") == 0) {
    s.enabled = (sub[1] == 'N');
    ok_text = s.enabled ? strings::kBotOnOk : strings::kBotOffOk;
  } else if (strcmp(sub, "CHANNEL") == 0) {
    char op[8];
    rest = extractCommand(rest, op, sizeof(op));
    char name[48];
    rest = extractToken(rest, name, sizeof(name));
    if (name[0] == 0) return copyOut(strings::kBotUsage, out, out_cap);

    if (strcmp(op, "ADD") == 0) {
      char psk_tok[64];
      extractToken(rest, psk_tok, sizeof(psk_tok));

      BotChannel c;
      if (name[0] == '#') {
        if (!normalizeHashtagName(name, c.name, sizeof(c.name))) return copyOut(strings::kBotBadName, out, out_cap);
        c.psk_len = 0;
      } else if (psk_tok[0] == 0) {
        if (!equalsIgnoreCase(name, "PUBLIC")) return copyOut(strings::kBotUsage, out, out_cap);
        c = BotSettings().channels[0];
      } else {
        if (!isValidKeyedName(name)) return copyOut(strings::kBotBadName, out, out_cap);
        uint8_t psk[kBotPskMaxLen];
        size_t n = base64Decode(psk_tok, psk, sizeof(psk));
        if (n != 16 && n != 32) return copyOut(strings::kDigestBadPsk, out, out_cap);
        strcpy(c.name, name);
        memcpy(c.psk, psk, n);
        c.psk_len = (uint8_t)n;
      }
      if (s.find(c.name) >= 0) return copyOut(strings::kBotChannelDuplicate, out, out_cap);
      if (s.count >= kBotMaxChannels) return copyOut(strings::kBotChannelFull, out, out_cap);
      s.channels[s.count++] = c;
      snprintf(buf, sizeof(buf), strings::kBotChannelAddedFmt, c.name);
    } else if (strcmp(op, "DEL") == 0) {
      int idx = s.find(name);
      if (idx < 0) return copyOut(strings::kBotChannelNotFound, out, out_cap);
      snprintf(buf, sizeof(buf), strings::kBotChannelRemovedFmt, s.channels[idx].name);
      for (size_t i = (size_t)idx; i + 1 < s.count; i++) s.channels[i] = s.channels[i + 1];
      s.count--;
      s.channels[s.count] = BotChannel();
    } else {
      return copyOut(strings::kBotUsage, out, out_cap);
    }
    ok_text = buf;
  } else {
    return copyOut(strings::kBotUsage, out, out_cap);
  }

  if (!ctx.bot.save(s)) return copyOut(strings::kBotSaveFailed, out, out_cap);
  return copyOut(ok_text, out, out_cap);
}

// --- News da MQTT (NEWS, solo amministratore) ---

// Host: 1-63 caratteri [A-Za-z0-9._-].
bool isValidNewsHost(const char* host) {
  size_t len = strlen(host);
  if (len == 0 || len >= kNewsServerLen) return false;
  for (size_t i = 0; i < len; i++) {
    char c = host[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
              c == '_';
    if (!ok) return false;
  }
  return true;
}

// Porta 1-65535, solo cifre.
bool parseNewsPort(const char* s, uint16_t& port) {
  if (s[0] == 0 || strlen(s) > 5) return false;
  uint32_t v = 0;
  for (const char* p = s; *p != 0; p++) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10 + (uint32_t)(*p - '0');
  }
  if (v == 0 || v > 65535) return false;
  port = (uint16_t)v;
  return true;
}

size_t handleNewsCmd(CommandContext& ctx, const char* args, UserId actor, char* out, size_t out_cap) {
  if (!hasAtLeastRole(ctx, actor, ROLE_ADMIN)) return copyOut(strings::kPermissionDenied, out, out_cap);

  char sub[8];
  const char* rest = extractCommand(args, sub, sizeof(sub));
  NewsSettings s = ctx.news.get();  // persisti-poi-applica: si modifica la copia
  char buf[BBS_MAX_TEXT_LEN + 1];

  if (sub[0] == 0) {
    // Mai la password ne' la chiave: solo se la chiave c'e'.
    const RoomInfo* room = (s.room_id == kNoRoom) ? nullptr : ctx.room_registry.findRoom(s.room_id);
    const NewsStatus& st = ctx.news.status();
    snprintf(buf, sizeof(buf), strings::kNewsStatusFmt, s.enabled ? strings::kNewsStatusOn : strings::kNewsStatusOff,
             s.server[0] ? s.server : strings::kNewsNotSet, (unsigned)s.port, s.topic,
             room ? room->name : strings::kNewsNotSet, s.key_len ? strings::kNewsYes : strings::kNewsNo,
             st.connected ? strings::kNewsConnected : strings::kNewsNotConnected, (unsigned)st.received_ok);
    return copyOut(buf, out, out_cap);
  }

  const char* ok_text = nullptr;
  if (strcmp(sub, "ON") == 0 || strcmp(sub, "OFF") == 0) {
    s.enabled = (sub[1] == 'N');
    ok_text = s.enabled ? strings::kNewsOnOk : strings::kNewsOffOk;
  } else if (strcmp(sub, "SERVER") == 0) {
    char host[kNewsServerLen + 8];
    rest = extractToken(rest, host, sizeof(host));
    char port_tok[8];
    extractToken(rest, port_tok, sizeof(port_tok));
    uint16_t port = kNewsDefaultPort;
    if (!isValidNewsHost(host) || (port_tok[0] != 0 && !parseNewsPort(port_tok, port))) {
      return copyOut(strings::kNewsBadServer, out, out_cap);
    }
    strcpy(s.server, host);
    s.port = port;
    snprintf(buf, sizeof(buf), strings::kNewsServerOkFmt, s.server, (unsigned)s.port);
    ok_text = buf;
  } else if (strcmp(sub, "USER") == 0) {
    char user[kNewsUserLen + 8];
    rest = extractToken(rest, user, sizeof(user));
    char pass[kNewsPassLen + 8];
    extractToken(rest, pass, sizeof(pass));
    if (strcmp(user, "-") == 0 && pass[0] == 0) {
      memset(s.username, 0, sizeof(s.username));
      memset(s.password, 0, sizeof(s.password));
      ok_text = strings::kNewsUserCleared;
    } else {
      if (user[0] == 0 || pass[0] == 0 || strlen(user) >= kNewsUserLen || strlen(pass) >= kNewsPassLen) {
        return copyOut(strings::kNewsBadUser, out, out_cap);
      }
      memset(s.username, 0, sizeof(s.username));
      memset(s.password, 0, sizeof(s.password));
      strcpy(s.username, user);
      strcpy(s.password, pass);
      ok_text = strings::kNewsUserOk;
    }
  } else if (strcmp(sub, "TOPIC") == 0) {
    char topic[kNewsTopicLen + 8];
    extractToken(rest, topic, sizeof(topic));
    if (topic[0] == 0 || strlen(topic) >= kNewsTopicLen) return copyOut(strings::kNewsBadTopic, out, out_cap);
    memset(s.topic, 0, sizeof(s.topic));
    strcpy(s.topic, topic);
    copyOut2(strings::kNewsTopicOkPrefix, s.topic, buf, sizeof(buf));
    ok_text = buf;
  } else if (strcmp(sub, "KEY") == 0) {
    char tok[64];
    extractToken(rest, tok, sizeof(tok));
    uint8_t key[kNewsKeyMaxLen];
    size_t n = base64Decode(tok, key, sizeof(key));
    if (n < kNewsKeyMinLen || n > kNewsKeyMaxLen) return copyOut(strings::kNewsBadKey, out, out_cap);
    memset(s.key, 0, sizeof(s.key));
    memcpy(s.key, key, n);
    s.key_len = (uint8_t)n;
    ok_text = strings::kNewsKeyOk;
  } else if (strcmp(sub, "ROOM") == 0) {
    uint8_t room_id;
    if (!parseRoomIdArg(ctx.room_registry, rest, room_id)) return copyOut(strings::kInvalidRoom, out, out_cap);
    s.room_id = room_id;
    copyOut2(strings::kNewsRoomOkPrefix, ctx.room_registry.findRoom(room_id)->name, buf, sizeof(buf));
    ok_text = buf;
  } else {
    return copyOut(strings::kNewsUsage, out, out_cap);
  }

  if (!ctx.news.save(s)) return copyOut(strings::kNewsSaveFailed, out, out_cap);
  return copyOut(ok_text, out, out_cap);
}

} // namespace

size_t processCommand(CommandContext& ctx, const char* input, char* out, size_t out_cap) {
  if (out_cap == 0) return 0;

  char cmd[16];
  const char* args = extractCommand(input, cmd, sizeof(cmd));

  UserId uid = ctx.users.findByPubkey(ctx.pub_key);
  bool registered = (uid != kInvalidUserId);

  if (!registered) {
    if (strcmp(cmd, "REGISTER") == 0) {
      if (!UserStore::isValidNickname(args)) {
        return copyOut(strings::kInvalidNickname, out, out_cap);
      }
      UserId new_id;
      RegisterResult r = ctx.users.registerUser(ctx.pub_key, args, ctx.now_ts, new_id);
      switch (r) {
        case RegisterResult::OK:
          // Appena registrato si e' gia' "online": senza questo, WHO/STATS
          // non ti conterebbero finche' non mandi un secondo comando.
          ctx.sessions.touch(new_id, ctx.now_ts);
          return copyOut2(strings::kRegisterOkPrefix, args, out, out_cap);
        case RegisterResult::ALREADY_EXISTS: {
          char nick[BBS_NICK_LEN];
          ctx.users.getNickname(ctx.users.findByPubkey(ctx.pub_key), nick, sizeof(nick));
          return copyOut2(strings::kAlreadyRegisteredPrefix, nick, out, out_cap);
        }
        case RegisterResult::INVALID_NICKNAME:
          return copyOut(strings::kInvalidNickname, out, out_cap);
        case RegisterResult::STORE_FULL:
          return copyOut(strings::kStoreFull, out, out_cap);
        default:
          return copyOut(strings::kStoreFull, out, out_cap);
      }
    }

    if (strcmp(cmd, "LOGIN") == 0) {
      return copyOut(strings::kLoginNoAccount, out, out_cap);
    }

    if (strcmp(cmd, "H") == 0) {
      return copyOut(strings::kHelpPreRegister, out, out_cap);
    }

    // Qualunque altro input da una chiave non registrata: benvenuto una
    // tantum, poi risposta breve.
    if (!ctx.welcome.isWelcomed(ctx.pub_key)) {
      ctx.welcome.markWelcomed(ctx.pub_key);
      return copyOut(strings::kWelcome, out, out_cap);
    }
    return copyOut(strings::kUnknownPreRegister, out, out_cap);
  }

  // Un utente bannato non riceve mai risposta, per nessun comando (ne'
  // notifiche: Notifier controlla isBanned per conto suo). Controllato
  // prima di registrare l'attivita' di sessione, cosi' un bannato non
  // risulta nemmeno "online".
  if (ctx.users.isBanned(uid)) {
    return 0;
  }

  // Utente registrato: ogni comando conta come attivita' della sessione
  // (LOGOUT la chiude subito dopo, vedi sotto).
  ctx.sessions.touch(uid, ctx.now_ts);

  if (strcmp(cmd, "REGISTER") == 0) {
    char nick[BBS_NICK_LEN];
    ctx.users.getNickname(uid, nick, sizeof(nick));
    return copyOut2(strings::kAlreadyRegisteredPrefix, nick, out, out_cap);
  }

  if (strcmp(cmd, "LOGIN") == 0) {
    ctx.users.touchLogin(uid, ctx.now_ts);

    // Se l'admin ha impostato un messaggio del giorno, ha la precedenza sul
    // conteggio dei non letti (un bollettino e' tipicamente piu' urgente;
    // i non letti restano comunque disponibili leggendo N/M).
    char motd[BBS_MAX_TEXT_LEN + 1];
    ctx.motd.get(motd, sizeof(motd));
    if (motd[0] != 0) {
      return copyOut(motd, out, out_cap);
    }

    uint32_t unread_posts = 0;
    uint8_t current_room = ctx.users.getCurrentRoom(uid);
    if (current_room != kNoRoom) {
      unread_posts = ctx.posts.countUnread(current_room, ctx.users.getLastRead(uid, current_room));
    }
    uint32_t unread_mail = ctx.mail.countUnread(uid, ctx.users.getLastMailRead(uid));

    char counts[64];
    snprintf(counts, sizeof(counts), strings::kLoginWelcomeBackFmt, (unsigned)unread_posts, (unsigned)unread_mail);
    return copyOut(counts, out, out_cap);
  }

  if (strcmp(cmd, "LOGOUT") == 0) {
    ctx.sessions.logout(uid);
    return copyOut(strings::kLogoutAck, out, out_cap);
  }

  if (strcmp(cmd, "H") == 0) {
    return copyOut(strings::kHelpPostRegister, out, out_cap);
  }

  if (strcmp(cmd, "K") == 0) {
    return handleRooms(ctx, uid, out, out_cap);
  }

  if (strcmp(cmd, "E") == 0) {
    return postToCurrentRoom(ctx, args, uid, out, out_cap);
  }

  if (strcmp(cmd, "N") == 0) {
    return handleNew(ctx, uid, out, out_cap);
  }

  if (strcmp(cmd, "M") == 0) {
    return handleMail(ctx, args, uid, out, out_cap);
  }

  if (strcmp(cmd, "S") == 0) {
    return handleJoinRoom(ctx, args, uid, out, out_cap);
  }

  if (strcmp(cmd, "U") == 0) {
    return handleLeaveRoom(ctx, uid, out, out_cap);
  }

  if (strcmp(cmd, "BAN") == 0) {
    return handleBanCmd(ctx, args, uid, true, out, out_cap);
  }

  if (strcmp(cmd, "UNBAN") == 0) {
    return handleBanCmd(ctx, args, uid, false, out, out_cap);
  }

  if (strcmp(cmd, "MUTE") == 0) {
    return handleMuteCmd(ctx, args, uid, true, out, out_cap);
  }

  if (strcmp(cmd, "UNMUTE") == 0) {
    return handleMuteCmd(ctx, args, uid, false, out, out_cap);
  }

  if (strcmp(cmd, "DELPOST") == 0) {
    return handleDelPostCmd(ctx, args, uid, out, out_cap);
  }

  if (strcmp(cmd, "CLOSE") == 0) {
    return handleRoomOpenClose(ctx, args, uid, true, out, out_cap);
  }

  if (strcmp(cmd, "OPEN") == 0) {
    return handleRoomOpenClose(ctx, args, uid, false, out, out_cap);
  }

  if (strcmp(cmd, "PIN") == 0) {
    return handlePinCmd(ctx, args, uid, out, out_cap);
  }

  if (strcmp(cmd, "UNPIN") == 0) {
    return handleUnpinCmd(ctx, args, uid, out, out_cap);
  }

  if (strcmp(cmd, "PINNED") == 0) {
    return handlePinnedCmd(ctx, uid, out, out_cap);
  }

  if (strcmp(cmd, "ROOM") == 0) {
    char sub[8];
    const char* rest = extractCommand(args, sub, sizeof(sub));
    if (strcmp(sub, "ADD") == 0) return handleRoomAddCmd(ctx, rest, uid, out, out_cap);
    if (strcmp(sub, "DEL") == 0) return handleRoomDelCmd(ctx, rest, uid, out, out_cap);
    return copyOut(strings::kRoomUsage, out, out_cap);
  }

  if (strcmp(cmd, "SETMOD") == 0) {
    return handleSetRoleCmd(ctx, args, uid, ROLE_MODERATOR, out, out_cap);
  }

  if (strcmp(cmd, "SETADMIN") == 0) {
    return handleSetRoleCmd(ctx, args, uid, ROLE_ADMIN, out, out_cap);
  }

  if (strcmp(cmd, "SETUSER") == 0) {
    return handleSetRoleCmd(ctx, args, uid, ROLE_USER, out, out_cap);
  }

  if (strcmp(cmd, "MODLOG") == 0) {
    return handleModLogCmd(ctx, uid, out, out_cap);
  }

  if (strcmp(cmd, "WHO") == 0) {
    return handleWho(ctx, out, out_cap);
  }

  if (strcmp(cmd, "STATS") == 0) {
    return handleStats(ctx, out, out_cap);
  }

  if (strcmp(cmd, "SEARCH") == 0) {
    return handleSearch(ctx, args, uid, out, out_cap);
  }

  if (strcmp(cmd, "MOTD") == 0) {
    return handleMotdCmd(ctx, args, uid, out, out_cap);
  }

  if (strcmp(cmd, "DIGEST") == 0) {
    return handleDigestCmd(ctx, args, uid, out, out_cap);
  }

  if (strcmp(cmd, "BOT") == 0) {
    return handleBotCmd(ctx, args, uid, out, out_cap);
  }

  if (strcmp(cmd, "NEWS") == 0) {
    return handleNewsCmd(ctx, args, uid, out, out_cap);
  }

  // Nessun comando riconosciuto: testo libero, trattato come post nella
  // stanza corrente (vedi postToCurrentRoom). Usa 'input' per intero, non
  // 'args': il primo token non e' un prefisso da scartare, fa parte del
  // messaggio (es. "ciao a tutti" non deve perdere "ciao").
  return postToCurrentRoom(ctx, input, uid, out, out_cap);
}

} // namespace bbs
