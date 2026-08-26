#include "StoreForward.h"
#include <helpers/ArduinoHelpers.h>

StoreForward::StoreForward() {
  _num_companions = 0;
  _head = 0;
  _count = 0;
  _recent_next = 0;
  memset(_recent_hashes, 0, sizeof(_recent_hashes));
  _last_advert_blob_len = 0;
  _has_last_advert = false;
  _enabled = false;
  _total_queued = _total_replayed = _total_evicted = 0;
  memset(_companions, 0, sizeof(_companions));
}

void StoreForward::structure() {
  def("en", _enabled);
  def("num", _num_companions);
  for (int i = 0; i < SF_MAX_COMPANIONS; i++) {
    char key[8];
    sprintf(key, "k%d", i);
    def(key, _companions[i].keyid, sizeof(_companions[i].keyid));
    char lkey[8];
    sprintf(lkey, "l%d", i);
    def(lkey, _companions[i].keyid_len);
  }
}

void StoreForward::load(FILESYSTEM* fs) {
  if (!fs->exists(SF_CFG_FILENAME)) return;
#if defined(RP2040_PLATFORM)
  File file = fs->open(SF_CFG_FILENAME, "r");
#else
  File file = fs->open(SF_CFG_FILENAME);
#endif
  if (file) {
    loadSerial(file);
    file.close();
    for (int i = 0; i < SF_MAX_COMPANIONS; i++) {
      if (_companions[i].keyid_len > SF_KEYID_MAX_LEN) _companions[i].keyid_len = 0;
    }
    if (_num_companions > SF_MAX_COMPANIONS) _num_companions = SF_MAX_COMPANIONS;
  }
}

void StoreForward::save(FILESYSTEM* fs) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove(SF_CFG_FILENAME);
  File file = fs->open(SF_CFG_FILENAME, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = fs->open(SF_CFG_FILENAME, "w");
#else
  File file = fs->open(SF_CFG_FILENAME, "w", true);
#endif
  if (file) {
    saveSerial(file);
    file.close();
  }
}

void StoreForward::setEnabled(bool en, FILESYSTEM* fs) {
  _enabled = en;
  save(fs);
}

bool StoreForward::setCompanionKeyIds(const char* csv_hex, FILESYSTEM* fs) {
  char buf[SF_MAX_COMPANIONS * (SF_KEYID_MAX_LEN * 2 + 1)];
  strncpy(buf, csv_hex, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;

  const char* parts[SF_MAX_COMPANIONS];
  int n = mesh::Utils::parseTextParts(buf, parts, SF_MAX_COMPANIONS, ',');

  SFCompanion parsed[SF_MAX_COMPANIONS];
  memset(parsed, 0, sizeof(parsed));
  for (int i = 0; i < n; i++) {
    int hex_len = strlen(parts[i]);
    int byte_len = hex_len / 2;
    if (hex_len == 0 || (hex_len & 1) != 0 || byte_len < SF_KEYID_MIN_LEN || byte_len > SF_KEYID_MAX_LEN) {
      return false;   // bad token, leave existing config untouched
    }
    if (!mesh::Utils::fromHex(parsed[i].keyid, byte_len, parts[i])) {
      return false;
    }
    parsed[i].keyid_len = byte_len;
    parsed[i].known = false;
  }

  memcpy(_companions, parsed, sizeof(_companions));
  _num_companions = n;
  save(fs);
  return true;
}

void StoreForward::formatKeyIdsReply(char* reply) const {
  if (_num_companions == 0) {
    strcpy(reply, "(none)");
    return;
  }
  char* p = reply;
  for (int i = 0; i < _num_companions; i++) {
    if (i > 0) *p++ = ',';
    char hex[SF_KEYID_MAX_LEN * 2 + 1];
    mesh::Utils::toHex(hex, _companions[i].keyid, _companions[i].keyid_len);
    strcpy(p, hex);
    p += strlen(hex);
    if (_companions[i].known) *p++ = '*';
  }
  *p = 0;
}

bool StoreForward::isRecentDup(const uint8_t* hash) {
  for (int i = 0; i < SF_RECENT_HASH_WINDOW; i++) {
    if (memcmp(_recent_hashes[i], hash, MAX_HASH_SIZE) == 0) return true;
  }
  return false;
}

void StoreForward::rememberHash(const uint8_t* hash) {
  memcpy(_recent_hashes[_recent_next], hash, MAX_HASH_SIZE);
  _recent_next = (_recent_next + 1) % SF_RECENT_HASH_WINDOW;
}

void StoreForward::enqueue(const mesh::Packet* pkt, uint8_t payload_type, uint8_t dest_hash) {
  uint8_t hash[MAX_HASH_SIZE];
  pkt->calculatePacketHash(hash);
  if (isRecentDup(hash)) return;
  rememberHash(hash);

  if (_count == SF_MAX_QUEUE) {
    // evict oldest to make room
    _head = (_head + 1) % SF_MAX_QUEUE;
    _count--;
    _total_evicted++;
  }
  uint16_t tail = (_head + _count) % SF_MAX_QUEUE;
  SFEntry& e = _queue[tail];
  e.blob_len = pkt->writeTo(e.blob);
  e.payload_type = payload_type;
  e.dest_hash = dest_hash;
  e.queued_at = millis();
  _count++;
  _total_queued++;
}

void StoreForward::maybeQueue(const mesh::Packet* pkt) {
  if (!_enabled || pkt->payload_len < 2) return;

  uint8_t type = pkt->getPayloadType();
  if (type == PAYLOAD_TYPE_TXT_MSG) {
    uint8_t dest_hash = pkt->payload[0];
    uint8_t src_hash = pkt->payload[1];

    // a companion that just sent this message is clearly online right now -
    // no need to hold a copy of its own outgoing traffic for later replay.
    for (int i = 0; i < _num_companions; i++) {
      if (_companions[i].known && _companions[i].hash_byte == src_hash) return;
    }

    for (int i = 0; i < _num_companions; i++) {
      if (_companions[i].known && _companions[i].hash_byte == dest_hash) {
        enqueue(pkt, type, dest_hash);
        break;
      }
    }
  } else if (type == PAYLOAD_TYPE_GRP_TXT) {
    enqueue(pkt, type, 0);
  }
}

void StoreForward::maybeStoreAdvert(const mesh::Packet* pkt, const mesh::Identity& id) {
  if (!_enabled) return;

  for (int i = 0; i < _num_companions; i++) {
    if (id.isHashMatch(_companions[i].keyid, _companions[i].keyid_len)) return;   // it's a companion, don't store
  }

  _last_advert_blob_len = pkt->writeTo(_last_advert_blob);
  _has_last_advert = true;
}

void StoreForward::onCompanionAdvert(const mesh::Identity& id, mesh::Dispatcher* dispatcher) {
  bool matched = false;
  uint8_t hash_byte = 0;
  for (int i = 0; i < _num_companions; i++) {
    SFCompanion& c = _companions[i];
    if (id.isHashMatch(c.keyid, c.keyid_len)) {
      matched = true;
      c.known = true;
      memcpy(c.pubkey, id.pub_key, PUB_KEY_SIZE);
      c.hash_byte = id.pub_key[0];
      hash_byte = c.hash_byte;
    }
  }
  if (!matched || !_enabled) return;

  if (_has_last_advert) {
    mesh::Packet* p = dispatcher->obtainNewPacket();
    if (p != NULL) {
      if (p->readFrom(_last_advert_blob, _last_advert_blob_len)) {
        dispatcher->sendPacket(p, 3, 0);   // pri=3, matches Mesh::sendFlood's de-prioritisation of ADVERT
        _total_replayed++;
      } else {
        dispatcher->releasePacket(p);
      }
    }
    _has_last_advert = false;
  }

  if (_count == 0) return;

  // In-place compaction: read and write pointers both walk the SAME circular
  // sequence starting at _head, so the write pointer (<=  read pointer) only
  // ever lands on a slot that has already been read this pass - no separate
  // buffer needed, and no risk of clobbering an as-yet-unread wrapped entry.
  uint16_t write_count = 0;
  for (uint16_t n = 0; n < _count; n++) {
    uint16_t src_i = (_head + n) % SF_MAX_QUEUE;
    SFEntry& e = _queue[src_i];
    bool is_match = (e.payload_type == PAYLOAD_TYPE_GRP_TXT) ||
                    (e.payload_type == PAYLOAD_TYPE_TXT_MSG && e.dest_hash == hash_byte);
    if (is_match) {
      mesh::Packet* p = dispatcher->obtainNewPacket();
      if (p != NULL) {
        if (p->readFrom(e.blob, e.blob_len)) {
          dispatcher->sendPacket(p, 1, 0);
          _total_replayed++;
        } else {
          dispatcher->releasePacket(p);
        }
      }
    } else {
      uint16_t dst_i = (_head + write_count) % SF_MAX_QUEUE;
      if (dst_i != src_i) {
        _queue[dst_i] = e;
      }
      write_count++;
    }
  }
  _count = write_count;   // _head is unchanged - kept entries still start there
}

void StoreForward::resetQueue() {
  _head = 0;
  _count = 0;
  _has_last_advert = false;
}

void StoreForward::formatStatsReply(char* reply) const {
  uint8_t known = 0;
  for (int i = 0; i < _num_companions; i++) {
    if (_companions[i].known) known++;
  }
  sprintf(reply, "en=%d, companions=%d (known=%d), queue=%d/%d, last_advert=%d, queued=%u, replayed=%u, evicted=%u",
          (int)_enabled, (int)_num_companions, (int)known, (int)_count, (int)SF_MAX_QUEUE,
          (int)_has_last_advert,
          (unsigned)_total_queued, (unsigned)_total_replayed, (unsigned)_total_evicted);
}
