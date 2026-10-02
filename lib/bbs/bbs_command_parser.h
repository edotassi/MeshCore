#pragma once

#include <cstddef>
#include <cstdint>

#include "bbs_mail_store.h"
#include "bbs_mod_log.h"
#include "bbs_motd.h"
#include "bbs_notifier.h"
#include "bbs_pending_welcome.h"
#include "bbs_post_store.h"
#include "bbs_room_registry.h"
#include "bbs_room_state.h"
#include "bbs_session_table.h"
#include "bbs_user_store.h"

namespace bbs {

struct CommandContext {
  UserStore& users;
  PendingWelcomeTable& welcome;
  PostStore& posts;
  MailStore& mail;
  SessionTable& sessions;
  Notifier& notifier;
  RoomState& rooms;
  RoomRegistry& room_registry;
  ModLog& modlog;
  MotdStore& motd;
  const uint8_t* pub_key;  // BBS_PUBKEY_LEN byte, del mittente
  uint32_t now_ts;
};

// Elabora 'input' (il comando ricevuto) e scrive la risposta null-terminata
// in 'out' (capacita' out_cap, che deve contenere almeno BBS_MAX_TEXT_LEN+1
// byte). Ritorna la lunghezza della risposta, senza il terminatore.
size_t processCommand(CommandContext& ctx, const char* input, char* out, size_t out_cap);

} // namespace bbs
