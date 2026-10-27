#pragma once

#include <cstddef>
#include <cstdint>

#include "bbs_port.h"

namespace bbs {

// Test bot: se qualcuno scrive "test" (o "!test") su uno dei canali di
// gruppo configurati, la BBS risponde sullo stesso canale con quanti hop ha
// fatto il messaggio e il percorso. Qui vive la parte pura (impostazioni
// persistite, riconoscimento del comando, formattazione della risposta,
// cooldown); la cifratura dei canali e l'invio radio sono nell'adattatore
// (bbs_port_adapter.h) e in MyMesh.

constexpr size_t kBotMaxChannels = 4;
constexpr size_t kBotNameLen = 24;  // terminatore incluso: nomi di 1-23 caratteri
constexpr size_t kBotNameMaxChars = kBotNameLen - 1;
constexpr size_t kBotPskMaxLen = 32;

// Intervallo minimo tra due risposte sullo stesso canale.
constexpr uint32_t kBotCooldownMs = 30000;

// Un canale del bot. Canale con chiave: psk_len 16 o 32 e 'psk' valida.
// Canale hashtag (nome che inizia con '#'): psk_len 0, la chiave si ricava
// dal nome nell'adattatore (SHA-256 non e' disponibile in lib/bbs).
struct BotChannel {
  char name[kBotNameLen];
  uint8_t psk[kBotPskMaxLen];
  uint8_t psk_len;  // 0 (hashtag), 16 o 32

  BotChannel();
  bool isHashtag() const { return psk_len == 0; }
};

struct BotSettings {
  bool enabled;
  BotChannel channels[kBotMaxChannels];
  uint8_t count;

  // Valori predefiniti: disattivato, canali "Public" e "#test".
  BotSettings();
  // Indice del canale con questo nome (senza distinzione di maiuscole), -1 se assente.
  int find(const char* name) const;
};

// Nome di canale hashtag: '#' + 1-22 caratteri [a-z0-9-]. L'input viene
// portato in minuscolo in 'out' (capacita' almeno kBotNameLen). false se non valido.
bool normalizeHashtagName(const char* in, char* out, size_t out_cap);

// Nome di canale con chiave: 1-23 caratteri stampabili, senza spazi, e non
// inizia con '#' (quelli sono hashtag).
bool isValidKeyedName(const char* name);

// Impostazioni persistite in un file a layout fisso e versionato, scritto
// campo per campo. File assente/corto/di versione ignota/con valori fuori
// range = default. Copia in RAM, aggiornata solo dopo una scrittura riuscita
// ("persisti-poi-applica", come DigestStore).
class BotStore {
public:
  explicit BotStore(IFileSystem& fs, const char* path = "/bbs/bot.dat");

  const BotSettings& get() const { return _settings; }
  bool save(const BotSettings& s);

  // Incrementato a ogni salvataggio riuscito: l'adattatore lo usa per sapere
  // quando ricalcolare le chiavi dei canali.
  uint32_t generation() const { return _generation; }

private:
  IFileSystem& _fs;
  const char* _path;
  BotSettings _settings;
  uint32_t _generation = 0;

  void load();
};

// Se 'text' ("<mittente>: <corpo>") ha come corpo esattamente "test" o
// "!test" (senza distinzione di maiuscole, spazi ai lati ignorati), copia il
// mittente (testo prima del primo ": ", troncato su un confine di code point
// UTF-8 se non entra) in 'sender_out' e ritorna true. Altrimenti false.
bool parseTestTrigger(const char* text, char* sender_out, size_t cap);

// Scrive in 'out' (al piu' cap-1 byte + terminatore) la risposta, senza
// prefisso del nodo: "@[mario] ricevuto con 3 hop via a1,7f,c2" oppure
// "@[mario] ricevuto diretto (0 hop)". Il percorso e' in esadecimale
// minuscolo, 'hash_size' byte (1-4) per hop. Se non entra toglie hop dalla
// coda e termina con ",..."; il nome del mittente e' accorciato su un
// confine UTF-8 solo se serve. Ritorna la lunghezza.
size_t formatHopReply(const char* sender, uint8_t hop_count, const uint8_t* path, uint8_t hash_size, char* out,
                      size_t cap);

// Cooldown per canale (indice nella tabella dei canali del bot).
class BotCooldown {
public:
  // true (e registra la risposta) se sul canale 'idx' l'ultima risposta e'
  // piu' vecchia di kBotCooldownMs; la prima e' sempre consentita. Gestisce
  // il ritorno a zero di millis() con sottrazione senza segno.
  bool allow(size_t idx, uint32_t now_ms);

private:
  uint32_t _last[kBotMaxChannels] = {};
  bool _used[kBotMaxChannels] = {};
};

} // namespace bbs
