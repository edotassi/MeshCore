#pragma once

#include <cstddef>
#include <cstdint>

#include "bbs_types.h"

namespace bbs {

enum class ModAction : uint8_t {
  BAN,
  UNBAN,
  MUTE,
  UNMUTE,
  DELPOST,
  CLOSE_ROOM,
  OPEN_ROOM,
  SET_MODERATOR,
  SET_ADMIN,
  SET_USER,
};

struct ModLogEntry {
  UserId actor_id;
  UserId target_id;  // kInvalidUserId se non applicabile (es. CLOSE/OPEN stanza)
  ModAction action;
  uint8_t room_id;  // usato solo da DELPOST/CLOSE_ROOM/OPEN_ROOM
  uint32_t timestamp;
};

// Registro delle azioni riservate (Fase 3). Solo RAM, circolare, a
// capacita' fissa: si perde al riavvio. Semplificazione deliberata — le
// azioni che conta (ban/mute/ruoli) sono gia' persistite dove serve
// davvero (UserStore); questo e' solo un log diagnostico "chi ha fatto
// cosa e quando" per la sessione corrente. Puo' diventare un log
// persistito in append (stesso schema di bbs_post_store) in futuro, senza
// cambiare questa interfaccia.
class ModLog {
public:
  void record(UserId actor, ModAction action, UserId target, uint8_t room_id, uint32_t now_ts);

  size_t count() const { return _count; }
  // 0 = la piu' vecchia tra quelle ancora presenti.
  const ModLogEntry& at(size_t i) const;

private:
  static constexpr size_t kCapacity = 32;
  ModLogEntry _entries[kCapacity];
  size_t _count = 0;
  size_t _next = 0;
};

} // namespace bbs
