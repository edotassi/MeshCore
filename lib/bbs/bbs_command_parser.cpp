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

enum class RoomPrefix { kNone, kValid, kInvalid };

// Se 'args' comincia con un token puramente numerico isolato (seguito da
// spazio o fine stringa), lo interpreta sempre come un id di stanza — valido
// (consuma il token, avanza 'args') o non valido (kInvalid, 'args' lasciato
// invariato ma il chiamante deve comunque segnalare l'errore, senza provare
// a interpretare il resto come testo). Se il primo carattere non e' una
// cifra, ritorna kNone e lascia 'args' invariato: e' testo libero. Nota:
// un post che dovesse iniziare con un numero isolato (es. "42 gradi oggi")
// va quindi scritto diversamente (es. "oggi ci sono 42 gradi") — limite
// noto e accettato di questa sintassi compatta.
RoomPrefix consumeOptionalRoomId(const char*& args, uint8_t& room_id) {
  if (args[0] < '0' || args[0] > '9') return RoomPrefix::kNone;
  const char* p = args;
  uint32_t v = 0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (uint32_t)(*p - '0');
    p++;
  }
  if (*p != ' ' && *p != 0) return RoomPrefix::kNone; // non e' un token numerico isolato
  if (v >= BBS_MAX_ROOMS || findRoom((uint8_t)v) == nullptr) return RoomPrefix::kInvalid;
  while (*p == ' ') p++;
  room_id = (uint8_t)v;
  args = p;
  return RoomPrefix::kValid;
}

size_t handleRooms(CommandContext& ctx, UserId uid, char* out, size_t out_cap) {
  size_t len = 0;
  for (size_t i = 0; i < kNumRooms && len + 1 < out_cap; i++) {
    if (i > 0 && len + 1 < out_cap) out[len++] = ' ';
    char id_buf[4];
    int n = snprintf(id_buf, sizeof(id_buf), "%u:", (unsigned)kRooms[i].id);
    for (int j = 0; j < n && len + 1 < out_cap; j++) out[len++] = id_buf[j];
    for (const char* p = kRooms[i].name; *p != 0 && len + 1 < out_cap; p++) out[len++] = *p;

    // "(da leggere/totale)": il totale conta tutti i post non cancellati
    // della stanza (0 come "ultimo letto" = nessun filtro); i da leggere
    // usano il puntatore "ultimo letto" di QUESTO utente per quella stanza.
    uint32_t total = ctx.posts.countUnread(kRooms[i].id, 0);
    uint32_t unread = ctx.posts.countUnread(kRooms[i].id, ctx.users.getLastRead(uid, kRooms[i].id));
    char count_buf[24];
    int cn = snprintf(count_buf, sizeof(count_buf), "(%u/%u)", (unsigned)unread, (unsigned)total);
    for (int j = 0; j < cn && len + 1 < out_cap; j++) out[len++] = count_buf[j];
  }
  out[len] = 0;
  return len;
}

size_t handlePost(CommandContext& ctx, const char* args, UserId uid, char* out, size_t out_cap) {
  uint8_t room_id = 0;
  if (consumeOptionalRoomId(args, room_id) == RoomPrefix::kInvalid) {
    return copyOut(strings::kInvalidRoom, out, out_cap);
  }
  const RoomInfo* room = findRoom(room_id);
  if (!room) return copyOut(strings::kInvalidRoom, out, out_cap);
  if (args[0] == 0) return copyOut(strings::kPostEmptyText, out, out_cap);

  if (ctx.users.isMuted(uid)) return copyOut(strings::kMutedCannotPost, out, out_cap);
  if (ctx.rooms.isClosed(room_id)) return copyOut(strings::kRoomClosed, out, out_cap);
  if (!ctx.sessions.allowMessage(uid, ctx.now_ts)) return copyOut(strings::kRateLimited, out, out_cap);

  if (!ctx.posts.appendPost(room_id, uid, ctx.now_ts, args)) {
    return copyOut(strings::kPostEmptyText, out, out_cap);
  }
  ctx.notifier.onNewPost(room_id, uid, ctx.now_ts);
  ctx.posts.enforceRetention(room_id, BBS_MAX_POSTS_PER_ROOM);
  return copyOut2(strings::kPostOkPrefix, room->name, out, out_cap);
}

size_t handleNew(CommandContext& ctx, const char* args, UserId uid, char* out, size_t out_cap) {
  uint8_t room_id = 0;
  if (consumeOptionalRoomId(args, room_id) == RoomPrefix::kInvalid) {
    return copyOut(strings::kInvalidRoom, out, out_cap);
  }
  const RoomInfo* room = findRoom(room_id);
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
bool parseRoomIdArg(const char* args, uint8_t& room_id) {
  if (args[0] < '0' || args[0] > '9') return false;
  const char* p = args;
  uint32_t v = 0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (uint32_t)(*p - '0');
    p++;
  }
  if (*p != 0 && *p != ' ') return false;
  if (v >= BBS_MAX_ROOMS || findRoom((uint8_t)v) == nullptr) return false;
  room_id = (uint8_t)v;
  return true;
}

// Come parseRoomIdArg, ma avanza 'args' oltre l'id e gli spazi seguenti
// (serve a SEARCH, che ha un argomento in piu' dopo la stanza).
bool parseRoomIdArgAdvance(const char*& args, uint8_t& room_id) {
  if (args[0] < '0' || args[0] > '9') return false;
  const char* p = args;
  uint32_t v = 0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (uint32_t)(*p - '0');
    p++;
  }
  if (*p != 0 && *p != ' ') return false;
  if (v >= BBS_MAX_ROOMS || findRoom((uint8_t)v) == nullptr) return false;
  while (*p == ' ') p++;
  room_id = (uint8_t)v;
  args = p;
  return true;
}

size_t handleSubscribe(CommandContext& ctx, const char* args, UserId uid, bool subscribe, char* out,
                        size_t out_cap) {
  uint8_t room_id;
  if (!parseRoomIdArg(args, room_id)) {
    return copyOut(strings::kInvalidRoom, out, out_cap);
  }
  ctx.users.setSubscribed(uid, room_id, subscribe);
  const RoomInfo* room = findRoom(room_id);
  return copyOut2(subscribe ? strings::kSubscribedPrefix : strings::kUnsubscribedPrefix, room->name, out, out_cap);
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
  if (!parseRoomIdArg(args, room_id)) return copyOut(strings::kInvalidRoom, out, out_cap);
  if (!ctx.posts.deleteLastPost(room_id)) return copyOut(strings::kDelPostNothingToDelete, out, out_cap);
  ctx.modlog.record(actor, ModAction::DELPOST, kInvalidUserId, room_id, ctx.now_ts);
  return copyOut2(strings::kDelPostOkPrefix, findRoom(room_id)->name, out, out_cap);
}

size_t handleRoomOpenClose(CommandContext& ctx, const char* args, UserId actor, bool close, char* out,
                            size_t out_cap) {
  if (!hasAtLeastRole(ctx, actor, ROLE_MODERATOR)) return copyOut(strings::kPermissionDenied, out, out_cap);
  uint8_t room_id;
  if (!parseRoomIdArg(args, room_id)) return copyOut(strings::kInvalidRoom, out, out_cap);
  ctx.rooms.setClosed(room_id, close);
  ctx.modlog.record(actor, close ? ModAction::CLOSE_ROOM : ModAction::OPEN_ROOM, kInvalidUserId, room_id,
                     ctx.now_ts);
  return copyOut2(close ? strings::kCloseOkPrefix : strings::kOpenOkPrefix, findRoom(room_id)->name, out, out_cap);
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
    if (any && len + 1 < out_cap) out[len++] = ' ';
    for (const char* p = nick; *p != 0 && len + 1 < out_cap; p++) out[len++] = *p;
    any = true;
  }
  if (!any) return copyOut(strings::kWhoNoOne, out, out_cap);
  out[len] = 0;
  return len;
}

size_t handleStats(CommandContext& ctx, char* out, size_t out_cap) {
  uint32_t total_posts = 0;
  for (size_t i = 0; i < kNumRooms; i++) {
    total_posts += ctx.posts.countUnread(kRooms[i].id, 0);
  }
  uint32_t total_mail = ctx.mail.totalCount();
  uint32_t total_users = ctx.users.count();
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

size_t handleSearch(CommandContext& ctx, const char* args, char* out, size_t out_cap) {
  uint8_t room_id;
  if (!parseRoomIdArgAdvance(args, room_id)) return copyOut(strings::kInvalidRoom, out, out_cap);
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
    for (size_t i = 0; i < kNumRooms; i++) {
      if (ctx.users.isSubscribed(uid, kRooms[i].id)) {
        unread_posts += ctx.posts.countUnread(kRooms[i].id, ctx.users.getLastRead(uid, kRooms[i].id));
      }
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
    return handlePost(ctx, args, uid, out, out_cap);
  }

  if (strcmp(cmd, "N") == 0) {
    return handleNew(ctx, args, uid, out, out_cap);
  }

  if (strcmp(cmd, "M") == 0) {
    return handleMail(ctx, args, uid, out, out_cap);
  }

  if (strcmp(cmd, "S") == 0) {
    return handleSubscribe(ctx, args, uid, true, out, out_cap);
  }

  if (strcmp(cmd, "U") == 0) {
    return handleSubscribe(ctx, args, uid, false, out, out_cap);
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
    return handleSearch(ctx, args, out, out_cap);
  }

  if (strcmp(cmd, "MOTD") == 0) {
    return handleMotdCmd(ctx, args, uid, out, out_cap);
  }

  return copyOut(strings::kUnknownPostRegister, out, out_cap);
}

} // namespace bbs
