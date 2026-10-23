#pragma once

#include <cstddef>
#include <cstdint>

#include "bbs_config.h"
#include "bbs_types.h"

namespace bbs {

// Sessioni attive, solo RAM (Fase 2): "chi e' online adesso", non "chi e'
// registrato" (quello e' UserStore, persistito). Array statico a capacita'
// fissa: quando pieno, la voce meno recentemente attiva viene sostituita
// (stesso principio di eviction gia' usato da ClientACL per i contatti).
class SessionTable {
public:
  SessionTable();

  // Crea la sessione se non esiste, altrimenti aggiorna l'orario di attivita'.
  void touch(UserId id, uint32_t now_ts);

  // Rimuove la sessione (LOGOUT esplicito).
  void logout(UserId id);

  bool isActive(UserId id, uint32_t now_ts, uint32_t timeout_secs = BBS_SESSION_TIMEOUT_SECS) const;

  size_t count() const;
  UserId userIdAt(size_t i) const;
  uint32_t lastActivityAt(size_t i) const;

  // Mancati recapiti: dopo BBS_MAX_MISSED_DELIVERIES consecutivi, la
  // sessione diventa irraggiungibile (niente piu' notifiche) finche' non
  // arriva un nuovo touch() (= l'utente ha ricontattato la BBS).
  bool isReachable(UserId id) const;
  void recordDeliverySuccess(UserId id);
  void recordDeliveryFailure(UserId id);

  // Anti-abuso (Fase 3): finestra scorrevole di 60 secondi. Ritorna false
  // (e non conta il messaggio) se l'utente ha gia' raggiunto il limite in
  // questa finestra. Se la sessione non esiste (non dovrebbe succedere,
  // touch() e' sempre chiamato prima), non blocca (fail open).
  bool allowMessage(UserId id, uint32_t now_ts, uint8_t max_per_minute = BBS_MAX_MESSAGES_PER_MINUTE);

private:
  struct Entry {
    bool used;
    UserId user_id;
    uint32_t last_activity_ts;
    uint8_t missed_deliveries;
    bool reachable;
    uint8_t messages_this_window;
    uint32_t window_start_ts;
  };

  int findIndex(UserId id) const;

  static constexpr size_t kCapacity = BBS_MAX_SESSIONS;
  Entry _entries[kCapacity];
};

} // namespace bbs
