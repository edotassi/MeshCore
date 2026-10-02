#pragma once

#include <cstdint>

#include "bbs_port.h"
#include "bbs_room_registry.h"
#include "bbs_session_table.h"
#include "bbs_user_store.h"

namespace bbs {

// Notifiche brevi di post nuovi (Fase 2): quando arriva un post, segna un
// "in sospeso" per ogni sessione attiva e raggiungibile iscritta a quella
// stanza (autore escluso); un giro periodico (tick, agganciato al traffico
// in arrivo, non a un timer dedicato) invia gli avvisi la cui finestra di
// accorpamento e' scaduta.
class Notifier {
public:
  Notifier(UserStore& users, SessionTable& sessions, RoomRegistry& rooms, IReplyChannel& reply);

  void onNewPost(uint8_t room_id, UserId author_id, uint32_t now_ts);

  void tick(uint32_t now_ts);

private:
  UserStore& _users;
  SessionTable& _sessions;
  RoomRegistry& _rooms;
  IReplyChannel& _reply;
};

} // namespace bbs
