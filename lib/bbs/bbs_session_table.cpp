#include "bbs_session_table.h"

namespace bbs {

SessionTable::SessionTable() {
  for (size_t i = 0; i < kCapacity; i++) {
    _entries[i].used = false;
    _entries[i].user_id = kInvalidUserId;
    _entries[i].last_activity_ts = 0;
    _entries[i].missed_deliveries = 0;
    _entries[i].reachable = true;
    _entries[i].messages_this_window = 0;
    _entries[i].window_start_ts = 0;
  }
}

int SessionTable::findIndex(UserId id) const {
  for (size_t i = 0; i < kCapacity; i++) {
    if (_entries[i].used && _entries[i].user_id == id) return (int)i;
  }
  return -1;
}

void SessionTable::touch(UserId id, uint32_t now_ts) {
  int idx = findIndex(id);
  bool is_new_slot = (idx < 0);
  if (idx < 0) {
    // cerca uno slot libero
    for (size_t i = 0; i < kCapacity; i++) {
      if (!_entries[i].used) {
        idx = (int)i;
        break;
      }
    }
  }
  if (idx < 0) {
    // nessuno slot libero: sostituisce il meno recentemente attivo
    size_t oldest = 0;
    for (size_t i = 1; i < kCapacity; i++) {
      if (_entries[i].last_activity_ts < _entries[oldest].last_activity_ts) oldest = i;
    }
    idx = (int)oldest;
  }
  if (is_new_slot) {
    // slot nuovo o riassegnato a un altro utente: nessuno stato residuo
    _entries[idx].messages_this_window = 0;
    _entries[idx].window_start_ts = 0;
  }
  _entries[idx].used = true;
  _entries[idx].user_id = id;
  _entries[idx].last_activity_ts = now_ts;
  // qualunque contatto azzera i mancati recapiti (torna "raggiungibile").
  _entries[idx].missed_deliveries = 0;
  _entries[idx].reachable = true;
}

void SessionTable::logout(UserId id) {
  int idx = findIndex(id);
  if (idx >= 0) _entries[idx].used = false;
}

bool SessionTable::isActive(UserId id, uint32_t now_ts, uint32_t timeout_secs) const {
  int idx = findIndex(id);
  if (idx < 0) return false;
  return (now_ts - _entries[idx].last_activity_ts) <= timeout_secs;
}

size_t SessionTable::count() const {
  size_t n = 0;
  for (size_t i = 0; i < kCapacity; i++) {
    if (_entries[i].used) n++;
  }
  return n;
}

UserId SessionTable::userIdAt(size_t i) const {
  size_t seen = 0;
  for (size_t k = 0; k < kCapacity; k++) {
    if (_entries[k].used) {
      if (seen == i) return _entries[k].user_id;
      seen++;
    }
  }
  return kInvalidUserId;
}

uint32_t SessionTable::lastActivityAt(size_t i) const {
  size_t seen = 0;
  for (size_t k = 0; k < kCapacity; k++) {
    if (_entries[k].used) {
      if (seen == i) return _entries[k].last_activity_ts;
      seen++;
    }
  }
  return 0;
}

bool SessionTable::isReachable(UserId id) const {
  int idx = findIndex(id);
  if (idx < 0) return false;
  return _entries[idx].reachable;
}

void SessionTable::recordDeliverySuccess(UserId id) {
  int idx = findIndex(id);
  if (idx < 0) return;
  _entries[idx].missed_deliveries = 0;
  _entries[idx].reachable = true;
}

void SessionTable::recordDeliveryFailure(UserId id) {
  int idx = findIndex(id);
  if (idx < 0) return;
  if (_entries[idx].missed_deliveries < 255) _entries[idx].missed_deliveries++;
  if (_entries[idx].missed_deliveries >= BBS_MAX_MISSED_DELIVERIES) _entries[idx].reachable = false;
}

bool SessionTable::allowMessage(UserId id, uint32_t now_ts, uint8_t max_per_minute) {
  int idx = findIndex(id);
  if (idx < 0) return true;  // fail open: non dovrebbe succedere (touch() precede sempre)

  Entry& e = _entries[idx];
  if (now_ts - e.window_start_ts >= 60) {
    e.window_start_ts = now_ts;
    e.messages_this_window = 0;
  }
  if (e.messages_this_window >= max_per_minute) return false;
  e.messages_this_window++;
  return true;
}

} // namespace bbs
