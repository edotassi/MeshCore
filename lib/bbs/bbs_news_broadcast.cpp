#include "bbs_news_broadcast.h"

#include <cstring>

namespace bbs {

namespace {

// Il piu' grande n <= max tale che il taglio a n byte non spezzi un code point.
size_t utf8SafeLen(const char* s, size_t len, size_t max) {
  if (len <= max) return len;
  size_t n = max;
  while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
  return n;
}

} // namespace

void NewsBroadcastQueue::push(const char* text) {
  if (text == nullptr) return;
  if (_count == kNewsBroadcastCapacity) {  // piena: via la piu' vecchia
    _head = (_head + 1) % kNewsBroadcastCapacity;
    _count--;
  }
  char* slot = _items[(_head + _count) % kNewsBroadcastCapacity];
  size_t n = utf8SafeLen(text, strlen(text), kNewsBroadcastTextLen - 1);
  memcpy(slot, text, n);
  slot[n] = 0;
  _count++;
}

bool NewsBroadcastQueue::poll(uint32_t now_ms, char* out, size_t cap) {
  if (_count == 0 || out == nullptr || cap == 0) return false;
  if (_sent_any && (uint32_t)(now_ms - _last_ms) < kNewsBroadcastIntervalMs) return false;

  const char* item = _items[_head];
  size_t n = utf8SafeLen(item, strlen(item), cap - 1);
  memcpy(out, item, n);
  out[n] = 0;

  _head = (_head + 1) % kNewsBroadcastCapacity;
  _count--;
  _last_ms = now_ms;
  _sent_any = true;
  return true;
}

} // namespace bbs
