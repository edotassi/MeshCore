#include "bbs_notifier.h"

#include <cstdio>
#include <cstring>

#include "bbs_room_registry.h"
#include "bbs_strings_it.h"

namespace bbs {

Notifier::Notifier(UserStore& users, SessionTable& sessions, IReplyChannel& reply)
    : _users(users), _sessions(sessions), _reply(reply) {}

void Notifier::onNewPost(uint8_t room_id, UserId author_id, uint32_t now_ts) {
  size_t n = _sessions.count();
  for (size_t i = 0; i < n; i++) {
    UserId uid = _sessions.userIdAt(i);
    if (uid == author_id) continue;
    if (!_sessions.isReachable(uid)) continue;
    if (_users.isBanned(uid)) continue;
    if (!_users.isSubscribed(uid, room_id)) continue;
    _sessions.addPendingPost(uid, room_id, now_ts);
  }
}

void Notifier::tick(uint32_t now_ts) {
  // _sessions.count()/userIdAt() cambiano posizione logica se una sessione
  // scade o si aggiunge durante l'iterazione: qui non succede (tick non
  // tocca touch/logout), quindi lo scan e' stabile.
  size_t n = _sessions.count();
  for (size_t i = 0; i < n; i++) {
    UserId uid = _sessions.userIdAt(i);

    for (size_t r = 0; r < kNumRooms; r++) {
      uint8_t room_id = kRooms[r].id;
      if (!_sessions.hasPendingReadyToFlush(uid, room_id, now_ts)) continue;

      uint16_t count = _sessions.consumePending(uid, room_id);
      if (count == 0) continue;
      if (_users.isBanned(uid)) continue;  // bannato dopo che il post era gia' in coda

      char msg[64];
      snprintf(msg, sizeof(msg), strings::kNewPostsNotifyFmt, (unsigned)count, kRooms[r].name);

      uint8_t pub_key[BBS_PUBKEY_LEN];
      if (!_users.getPubkey(uid, pub_key)) continue;

      bool ok = _reply.sendReply(pub_key, (const uint8_t*)msg, strlen(msg));
      if (ok) {
        _sessions.recordDeliverySuccess(uid);
      } else {
        _sessions.recordDeliveryFailure(uid);
      }
    }
  }
}

} // namespace bbs
