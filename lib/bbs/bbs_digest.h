#pragma once

#include <cstddef>
#include <cstdint>

#include "bbs_port.h"
#include "bbs_post_store.h"
#include "bbs_room_registry.h"

namespace bbs {

// Riepilogo giornaliero (digest): una volta al giorno, all'ora locale
// configurata, la BBS manda UN messaggio di gruppo con il riassunto dei post
// delle ultime 24 ore. Qui vive la parte pura (impostazioni persistite,
// calcolo dell'ora locale con ora legale UE, decisione "e' ora di
// inviare?", formattazione del testo); l'invio radio e' nell'adattatore
// (bbs_port_adapter.h) e in MyMesh.

// Primo istante considerato un orologio valido (2024-01-01 00:00 UTC): prima
// di allora l'RTC non e' stato sincronizzato e non si invia nulla.
constexpr uint32_t kDigestMinValidEpoch = 1704067200u;

// Oltre questo ritardo rispetto all'ora programmata (es. nodo acceso alle
// 23:00) il riepilogo del giorno viene saltato invece di partire in ritardo.
constexpr uint32_t kDigestCatchUpWindowMin = 120;

constexpr size_t kDigestPskMaxLen = 32;

// PSK del canale Public di MeshCore, in base64 (16 byte).
constexpr const char* kDigestPublicPskBase64 = "izOH6cXN6mrJ5e26oRXNcg==";

// Decodifica base64 standard (padding '=' opzionale, nessuno spazio).
// Ritorna il numero di byte scritti in 'out', oppure 0 se l'input non e'
// valido o non entra in 'out_cap'. Senza allocazioni.
size_t base64Decode(const char* in, uint8_t* out, size_t out_cap);

// Chiave di canale/PSK come la mostrano le app: esadecimale (32 o 64 cifre,
// cioe' 16 o 32 byte) oppure base64. Nessuna ambiguita' per le lunghezze
// ammesse: 32/64 caratteri in base64 darebbero 24/48 byte. Ritorna i byte
// decodificati, 0 se non valida o se non entra in out_cap.
size_t decodeKey(const char* in, uint8_t* out, size_t out_cap);

struct DigestSettings {
  bool enabled;
  uint8_t hour;            // ora locale, 0-23
  uint8_t minute;          // 0-59
  int16_t tz_offset_min;   // fuso rispetto a UTC, in minuti (-720..+840)
  bool eu_dst;             // applica l'ora legale UE (+60 min)
  uint8_t psk[kDigestPskMaxLen];
  uint8_t psk_len;         // 16 o 32
  uint32_t last_sent_day;  // giorno locale (giorni dall'epoch) dell'ultimo invio/salto; 0 = mai

  // Valori predefiniti: disattivato, 08:00, CET (+1) con ora legale UE,
  // canale Public.
  DigestSettings();
  void setPublicPsk();
  bool usesPublicPsk() const;
};

enum class DigestDecision { WAIT, SEND, SKIP_DAY };

// --- Tempo locale (funzioni pure) ---

// true se in 'utc_ts' vige l'ora legale UE: dall'ultima domenica di marzo
// alle 01:00 UTC all'ultima domenica di ottobre alle 01:00 UTC.
bool isEuDst(uint32_t utc_ts);

// Secondi dall'epoch in ora locale (fuso + eventuale ora legale).
int64_t localTime(uint32_t utc_ts, const DigestSettings& s);

// Giorni dall'epoch (locale) e minuti dalla mezzanotte, da un'ora locale.
uint32_t localDayNumber(int64_t local_ts);
uint32_t localMinutesOfDay(int64_t local_ts);

// Data civile (gregoriana) da un numero di giorni dall'epoch.
void civilFromDays(int64_t days, int& year, unsigned& month, unsigned& day);

DigestDecision decide(const DigestSettings& s, uint32_t now_utc);

// Impostazioni persistite in un file a layout fisso e versionato, scritto
// campo per campo. File assente/corto/di versione ignota/con valori fuori
// range = default. Copia in RAM, aggiornata solo dopo una scrittura riuscita
// ("persisti-poi-applica", come RoomRegistry).
class DigestStore {
public:
  explicit DigestStore(IFileSystem& fs, const char* path = "/bbs/digest.dat");

  const DigestSettings& get() const { return _settings; }
  bool save(const DigestSettings& s);

  // Richiesta di invio immediato (DIGEST NOW): solo RAM, non persistita.
  void requestNow() { _now_requested = true; }
  bool takeNowRequest() {
    bool r = _now_requested;
    _now_requested = false;
    return r;
  }

private:
  IFileSystem& _fs;
  const char* _path;
  DigestSettings _settings;
  bool _now_requested = false;

  void load();
};

// Scrive in 'out' (al piu' cap-1 byte + terminatore) il testo del riepilogo,
// senza prefisso mittente. Ritorna la lunghezza, 0 se negli ultimi 24 ore
// non ci sono post (out resta vuoto). Contiene solo il totale e i conteggi
// per stanza, mai testo o autori dei post. Se non entra, accorcia l'elenco
// delle stanze su un confine di code point UTF-8.
size_t formatDigest(PostStore& posts, RoomRegistry& rooms, uint32_t now_utc, char* out, size_t cap);

} // namespace bbs
