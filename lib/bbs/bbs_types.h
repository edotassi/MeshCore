#pragma once

#include <cstdint>

#include "bbs_config.h"

namespace bbs {

using UserId = uint32_t;
constexpr UserId kInvalidUserId = 0xFFFFFFFFu;

enum UserRole : uint8_t {
  ROLE_USER = 0,
  ROLE_MODERATOR = 1,
  ROLE_ADMIN = 2,
};

// Bit di UserRecord::flags (Fase 3, moderazione).
constexpr uint8_t kUserFlagBanned = 0x01;
constexpr uint8_t kUserFlagMuted = 0x02;

// Rappresentazione in memoria di un post letto/scritto dal log di una
// stanza (vedi bbs_post_store.h/.cpp per il formato esatto su file).
// Bit di PostRecord::flags.
constexpr uint8_t kPostFlagDeleted = 0x01;
constexpr uint8_t kPostFlagPinned = 0x02;  // Fase 6: post fissati, vedi bbs_post_store.h

struct PostRecord {
  uint8_t  version;
  uint8_t  room_id;
  uint32_t timestamp;
  UserId   user_id;
  uint16_t text_len;
  uint8_t  flags;
  char     text[BBS_MAX_TEXT_LEN + 1];
};

// Rappresentazione in memoria di una mail privata (vedi bbs_mail_store.h/.cpp
// per il formato esatto su file).
struct MailRecord {
  uint8_t  version;
  UserId   sender_id;
  UserId   recipient_id;
  uint32_t timestamp;
  uint16_t text_len;
  uint8_t  flags;
  char     text[BBS_MAX_TEXT_LEN + 1];
};

// Rappresentazione in memoria di un record utente. Il formato su file e'
// scritto/letto campo per campo (vedi bbs_user_store.cpp), mai con memcpy
// di questo struct, per non dipendere dal padding del compilatore.
struct UserRecord {
  uint8_t  pub_key[BBS_PUBKEY_LEN];
  char     nickname[BBS_NICK_LEN];
  uint8_t  role;
  uint8_t  flags;
  uint32_t created_ts;
  uint32_t last_login_ts;
  uint32_t last_read[BBS_MAX_ROOMS];
  uint32_t last_mail_read_seq;
  uint8_t  subscribed_rooms;  // bitmask: bit room_id = iscritto a quella stanza (vedi bbs_room_registry.h)
  uint8_t  reserved[9];
};

} // namespace bbs
