#pragma once

#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <helpers/ConfigSerializer.h>

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
  using File = fs::File;
#endif

#include <helpers/IdentityStore.h>   // defines the FILESYSTEM type
#include <target.h>

#ifndef SF_MAX_COMPANIONS
  #define SF_MAX_COMPANIONS    8
#endif
#define SF_KEYID_MIN_LEN       2   // bytes
#define SF_KEYID_MAX_LEN       8   // bytes

#ifndef SF_MAX_QUEUE
  #define SF_MAX_QUEUE         500
#endif

#define SF_MAX_BLOB_LEN         MAX_TRANS_UNIT   // raw Packet::writeTo() blob, worst case

#define SF_RECENT_HASH_WINDOW   16   // for de-duping flood re-broadcasts of the same message

#ifndef SF_MAX_ADVERTS
  #define SF_MAX_ADVERTS       64   // max distinct non-companion identities cached at once
#endif

// defaults (CLI-configurable at runtime via 'storeforward ttl msg|advert <secs>', persisted)
#ifndef SF_MSG_TTL_MS
  #define SF_MSG_TTL_MS        (24UL * 3600UL * 1000UL)   // 24h - direct/channel messages older than this are dropped, never replayed
#endif
#ifndef SF_ADVERT_TTL_MS
  #define SF_ADVERT_TTL_MS     (3600UL * 1000UL)          // 1h - cached adverts older than this are dropped, never replayed
#endif
#define SF_MIN_TTL_SECS         10U          // sanity floor for CLI-set TTLs
#define SF_MAX_TTL_SECS         4000000UL    // ~46 days - keeps ttl_ms comfortably inside uint32_t

#define SF_CFG_FILENAME  "/sf_cfg"

struct SFCompanion {
  uint8_t keyid[SF_KEYID_MAX_LEN];
  uint8_t keyid_len;
  bool    known;              // true once we've matched an advert from this companion
  uint8_t pubkey[PUB_KEY_SIZE];
  uint8_t hash_byte;          // == pubkey[0], the 1-byte dest hash this companion appears as on the wire
};

struct SFEntry {
  uint8_t  blob[SF_MAX_BLOB_LEN];
  uint8_t  blob_len;
  uint8_t  payload_type;      // PAYLOAD_TYPE_TXT_MSG or PAYLOAD_TYPE_GRP_TXT
  uint8_t  dest_hash;         // only meaningful for TXT_MSG
  uint32_t queued_at;
};

struct SFAdvertEntry {
  uint8_t  blob[SF_MAX_BLOB_LEN];
  uint8_t  blob_len;
  uint8_t  pubkey[PUB_KEY_SIZE];   // identity this advert came from - used for dedup
  uint32_t queued_at;
};

/**
 * \brief  Repeater-only feature: holds direct messages addressed to a configured list of
 *     "companion" nodes, and channel messages, in a RAM ring buffer, then replays them
 *     over LoRa when a matching companion's advert is heard again.
 */
class StoreForward : public ConfigSerializer {
  SFCompanion _companions[SF_MAX_COMPANIONS];
  uint8_t _num_companions;

  SFEntry _queue[SF_MAX_QUEUE];
  uint16_t _head;    // ring index of oldest occupied slot
  uint16_t _count;   // number of occupied slots

  uint8_t _recent_hashes[SF_RECENT_HASH_WINDOW][MAX_HASH_SIZE];
  uint8_t _recent_next;

  // per-identity advert cache: one entry per unique non-companion node seen,
  // deduped by full pubkey. Same FIFO ring-buffer + oldest-eviction scheme as
  // the message queue.
  SFAdvertEntry _adverts[SF_MAX_ADVERTS];
  uint16_t _advert_head;
  uint16_t _advert_count;
  uint32_t _total_adverts_queued, _total_adverts_replayed, _total_adverts_evicted, _total_adverts_expired;

  bool _enabled;

  uint32_t _msg_ttl_ms;
  uint32_t _advert_ttl_ms;

  uint32_t _total_queued, _total_replayed, _total_evicted, _total_expired;

  static bool isExpired(uint32_t queued_at, uint32_t ttl_ms) { return (uint32_t)(millis() - queued_at) > ttl_ms; }

  bool isRecentDup(const uint8_t* hash);
  void rememberHash(const uint8_t* hash);
  void enqueue(const mesh::Packet* pkt, uint8_t payload_type, uint8_t dest_hash);
  void pruneExpiredMessages();   // drops expired entries from the head of _queue (strictly FIFO-ordered)
  void pruneExpiredAdverts();    // full-scan compaction, since in-place refresh breaks strict ordering
  int findAdvertSlot(const uint8_t* pubkey) const;   // -1 if not present

protected:
  void structure() override;

public:
  StoreForward();

  void load(FILESYSTEM* fs);
  void save(FILESYSTEM* fs);

  bool isEnabled() const { return _enabled; }
  void setEnabled(bool en, FILESYSTEM* fs);

  uint32_t getMsgTtlSecs() const { return _msg_ttl_ms / 1000; }
  uint32_t getAdvertTtlSecs() const { return _advert_ttl_ms / 1000; }
  // returns false if secs is outside [SF_MIN_TTL_SECS, SF_MAX_TTL_SECS]
  bool setMsgTtlSecs(uint32_t secs, FILESYSTEM* fs);
  bool setAdvertTtlSecs(uint32_t secs, FILESYSTEM* fs);

  // csv_hex: comma separated hex strings, each SF_KEYID_MIN_LEN..SF_KEYID_MAX_LEN bytes. Replaces the list.
  // returns false if any token is malformed (nothing is changed in that case).
  bool setCompanionKeyIds(const char* csv_hex, FILESYSTEM* fs);
  void formatKeyIdsReply(char* reply) const;

  // called from MyMesh::onRecvPacket(), before normal dispatch. Non-destructive peek at the packet.
  void maybeQueue(const mesh::Packet* pkt);

  // called from MyMesh::onAdvertRecv(), for every advert (companion or not).
  // Caches the packet, keyed by the sender's identity, only if 'id' does NOT
  // match a configured companion. An existing entry for the same identity is
  // updated in place; otherwise a new one is added (evicting the oldest if full).
  void maybeStoreAdvert(const mesh::Packet* pkt, const mesh::Identity& id);

  // called from MyMesh::onAdvertRecv(). Replays (and frees) all queued entries matching this companion,
  // via dispatcher->obtainNewPacket()/sendPacket().
  void onCompanionAdvert(const mesh::Identity& id, mesh::Dispatcher* dispatcher);

  void resetQueue();   // clears the queue only (companion list + enabled flag untouched)

  void formatStatsReply(char* reply) const;
};
