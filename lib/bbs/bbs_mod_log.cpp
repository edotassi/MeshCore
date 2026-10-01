#include "bbs_mod_log.h"

namespace bbs {

void ModLog::record(UserId actor, ModAction action, UserId target, uint8_t room_id, uint32_t now_ts) {
  ModLogEntry& e = _entries[_next];
  e.actor_id = actor;
  e.target_id = target;
  e.action = action;
  e.room_id = room_id;
  e.timestamp = now_ts;

  _next = (_next + 1) % kCapacity;
  if (_count < kCapacity) _count++;
}

const ModLogEntry& ModLog::at(size_t i) const {
  // La piu' vecchia tra quelle presenti e' _next se il buffer e' pieno
  // (perche' _next e' il prossimo slot da sovrascrivere = il piu' vecchio),
  // altrimenti e' semplicemente indice 0 (mai ancora girato).
  size_t start = (_count < kCapacity) ? 0 : _next;
  return _entries[(start + i) % kCapacity];
}

} // namespace bbs
