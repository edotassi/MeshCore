#include "bbs_notifier.h"

#include <cstring>

namespace bbs {

Notifier::Notifier(UserStore& users, SessionTable& sessions, RoomRegistry& rooms, PostStore& posts,
                    IReplyChannel& reply)
    : _users(users), _sessions(sessions), _rooms(rooms), _posts(posts), _reply(reply) {}

void Notifier::tick(uint32_t now_ts) {
  // _sessions.count()/userIdAt() cambiano posizione logica se una sessione
  // scade o si aggiunge durante l'iterazione: qui non succede (tick non
  // tocca touch/logout), quindi lo scan e' stabile.
  size_t n = _sessions.count();
  for (size_t i = 0; i < n; i++) {
    UserId uid = _sessions.userIdAt(i);
    if (!_sessions.isActive(uid, now_ts)) continue;
    if (!_sessions.isReachable(uid)) continue;
    if (_users.isBanned(uid)) continue;

    uint8_t room_id = _users.getCurrentRoom(uid);
    if (room_id == kNoRoom) continue;
    if (_rooms.findRoom(room_id) == nullptr) continue;  // stanza cancellata nel frattempo

    uint32_t after_ts = _users.getLastRead(uid, room_id);
    PostRecord post;
    if (!_posts.findNextUnread(room_id, after_ts, post)) continue;

    if (post.user_id == uid) {
      // Non si notifica l'autore del proprio post, ma si avanza comunque il
      // puntatore: altrimenti questo stesso post bloccherebbe ogni tick
      // successivo per sempre.
      _users.setLastRead(uid, room_id, post.timestamp);
      continue;
    }

    uint8_t author_pub_key[BBS_PUBKEY_LEN];
    if (!_users.getPubkey(post.user_id, author_pub_key)) continue;

    char author_nick[BBS_NICK_LEN];
    if (!_users.getNickname(post.user_id, author_nick, sizeof(author_nick))) {
      strncpy(author_nick, "???", sizeof(author_nick));
      author_nick[sizeof(author_nick) - 1] = 0;
    }

    char msg[BBS_MAX_TEXT_LEN + 1];
    size_t len = 0;
    for (const char* p = author_nick; *p != 0 && len + 1 < sizeof(msg); p++) msg[len++] = *p;
    for (const char* p = ": "; *p != 0 && len + 1 < sizeof(msg); p++) msg[len++] = *p;
    for (const char* p = post.text; *p != 0 && len + 1 < sizeof(msg); p++) msg[len++] = *p;
    msg[len] = 0;

    uint8_t recipient_pub_key[BBS_PUBKEY_LEN];
    if (!_users.getPubkey(uid, recipient_pub_key)) continue;

    bool ok = _reply.sendAuthoredReply(recipient_pub_key, author_pub_key, (const uint8_t*)msg, len);
    if (ok) {
      _users.setLastRead(uid, room_id, post.timestamp);
      _sessions.recordDeliverySuccess(uid);
    } else {
      _sessions.recordDeliveryFailure(uid);  // si riprova al prossimo tick, last_read non avanza
    }
  }
}

} // namespace bbs
