#pragma once

#include <cstddef>
#include <cstdint>

#include "bbs_port.h"

// Tabella dei client che hanno gia' ricevuto il MOTD: evita di rimandarlo a
// ogni login. Parte pura (solo bbs::IFileSystem, CRC16 e I/O binario della BBS).
// File in /room/, mai /bbs/.

namespace room {

constexpr size_t kMotdSeenCapacity = 256;
constexpr size_t kMotdKeyPrefixLen = 8;
constexpr uint32_t kMotdResendSecs = 24u * 3600u;

constexpr const char* kMotdSeenPath = "/room/motd_seen.dat";
constexpr const char* kMotdSeenTmpPath = "/room/motd_seen.tmp";

// FNV-1a a 32 bit, stabile tra build.
uint32_t motdHash(const char* text);

class MotdSeen {
public:
  explicit MotdSeen(bbs::IFileSystem& fs) : _fs(fs) {}

  // true se il client non ha mai ricevuto questo testo, o l'ultimo invio e'
  // di almeno 24 h fa. Se l'orologio e' andato indietro (now < last_sent) e
  // il testo e' lo stesso: false.
  bool shouldSend(const uint8_t* key_prefix, uint32_t motd_hash, uint32_t now) const;
  // Inserisce/aggiorna; a tabella piena sostituisce la voce con last_sent piu' vecchio.
  void markSent(const uint8_t* key_prefix, uint32_t motd_hash, uint32_t now);
  void clear();

  bool dirty() const { return _dirty; }
  size_t count() const { return _count; }

  // Carica (assente/garbage/versione ignota = tabella vuota; se manca il file
  // principale ripiega sul .tmp). Salva via tmp + remove + rename; azzera dirty se riuscito.
  void load();
  bool save();

private:
  struct Entry {
    uint8_t key[kMotdKeyPrefixLen];
    uint32_t hash;
    uint32_t last_sent;
  };

  bbs::IFileSystem& _fs;
  Entry _entries[kMotdSeenCapacity];
  size_t _count = 0;
  bool _dirty = false;

  int find(const uint8_t* key_prefix) const;
  bool loadFrom(const char* path);
};

} // namespace room
