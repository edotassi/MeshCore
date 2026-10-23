#pragma once

#include <cstdint>

#include "bbs_port.h"
#include "bbs_post_store.h"
#include "bbs_room_registry.h"
#include "bbs_session_table.h"
#include "bbs_user_store.h"

namespace bbs {

// Push proattivo dei post (Fase 2, riscritta): per ogni sessione online e
// raggiungibile, se la sua stanza corrente (UserStore::getCurrentRoom) ha
// un post con timestamp maggiore del suo ultimo letto, lo spinge per
// intero (autore corretto incluso) senza che l'utente lo richieda con N.
// Un solo post per utente per chiamata di tick(): l'arretrato si consegna
// gradualmente nel tempo, un post alla volta, non tutto insieme (stesso
// principio del Room Server originale di MeshCore). tick() e' agganciata
// al traffico in arrivo (chiamata da bbs_port_adapter ad ogni messaggio
// ricevuto dalla BBS), non a un timer dedicato.
class Notifier {
public:
  Notifier(UserStore& users, SessionTable& sessions, RoomRegistry& rooms, PostStore& posts, IReplyChannel& reply);

  void tick(uint32_t now_ts);

private:
  UserStore& _users;
  SessionTable& _sessions;
  RoomRegistry& _rooms;
  PostStore& _posts;
  IReplyChannel& _reply;
};

} // namespace bbs
