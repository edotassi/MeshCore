#pragma once

#include <cstddef>
#include <cstdint>

#include "bbs_config.h"

// Interfacce astratte pure: nessuna dipendenza da Arduino o dalle classi
// MeshCore. Le implementazioni concrete (LittleFS, RTCClock, invio messaggi
// diretti) vivono in lib/bbs/port/ — l'unico punto che parla con MeshCore.

namespace bbs {

class IClock {
public:
  virtual ~IClock() = default;
  virtual uint32_t now() const = 0;
  virtual uint32_t nowUnique() const = 0;
};

class IFile {
public:
  virtual ~IFile() = default;
  virtual size_t read(uint8_t* buf, size_t len) = 0;
  virtual size_t write(const uint8_t* buf, size_t len) = 0;
  virtual bool seek(uint32_t offset) = 0;
  virtual uint32_t size() const = 0;
  virtual void close() = 0;
  virtual bool valid() const = 0;
};

class IFileSystem {
public:
  virtual ~IFileSystem() = default;
  // mode: 'r' lettura, 'a' append (crea se assente), 'w' crea/tronca.
  virtual IFile* open(const char* path, char mode) = 0;
  // remove/rename servono per l'aggiornamento in-place di un record (vedi
  // UserStore::touchLogin): si scrive un file temporaneo e lo si rinomina
  // sopra l'originale, senza mai aprire lo stesso file in lettura e
  // scrittura contemporaneamente.
  virtual bool remove(const char* path) = 0;
  virtual bool rename(const char* from, const char* to) = 0;
};

// Invio asincrono a un utente identificato dalla sua chiave pubblica (non
// dal contesto di un messaggio in arrivo): usato dal Notifier per le
// notifiche di post nuovi. Ritorna false se il peer non e' raggiungibile
// (es. mai visto, o evitto dall'ACL) — il chiamante lo conta come mancato
// recapito (vedi SessionTable::recordDeliveryFailure).
class IReplyChannel {
public:
  virtual ~IReplyChannel() = default;
  virtual bool sendReply(const uint8_t pub_key[BBS_PUBKEY_LEN], const uint8_t* payload, size_t len) = 0;
};

} // namespace bbs
