#pragma once

#include <cstddef>
#include <cstdint>

#include "bbs_config.h"
#include "bbs_port.h"
#include "bbs_post_store.h"
#include "bbs_room_registry.h"
#include "bbs_types.h"
#include "bbs_user_store.h"

namespace bbs {

// News da MQTT: uno script su un VPS pubblica titoli di giornale su un broker
// MQTT; la BBS (solo l'ambiente firmware con WiFi) li riceve, ne verifica la
// firma HMAC, scarta i doppioni e li accoda come post in una stanza
// configurata ("News"), con autore un utente di sistema. Qui vive la parte
// pura (impostazioni persistite, parsing e verifica del messaggio, anti
// doppioni, accodamento); la connessione MQTT e' nel firmware
// (examples/bbs_room_server/BbsNewsClient.*), il collegamento nell'adattatore
// (bbs_port_adapter.h).
//
// Formato del payload MQTT (UTF-8, esattamente 6 righe separate da '\n', un
// '\n' finale e' tollerato):
//   v1
//   <id>      1-32 caratteri [0-9a-zA-Z_-]
//   <ts>      epoch Unix in secondi (decimale) della pubblicazione
//   <fonte>   1-16 byte, senza a capo (es. ANSA)
//   <titolo>  1-140 byte UTF-8, senza a capo
//   <hmac>    64 caratteri esadecimali minuscoli: HMAC-SHA256(chiave, le
//             prime cinque righe unite da '\n', cioe' "v1\n<id>\n<ts>\n<fonte>\n<titolo>")
// MQTT viaggia in chiaro: senza la firma chiunque potrebbe pubblicare sul
// topic e riempire la stanza.

constexpr size_t kNewsServerLen = 64;  // terminatore incluso
constexpr size_t kNewsUserLen = 32;
constexpr size_t kNewsPassLen = 32;
constexpr size_t kNewsTopicLen = 64;
constexpr size_t kNewsKeyMaxLen = 32;
constexpr size_t kNewsKeyMinLen = 16;
constexpr uint16_t kNewsDefaultPort = 1883;
constexpr const char* kNewsDefaultTopic = "bbs/news";

constexpr size_t kNewsIdMaxLen = 32;
constexpr size_t kNewsSourceMaxLen = 16;
constexpr size_t kNewsTitleMaxLen = 140;

// Ricezioni oltre questa lunghezza non possono essere valide.
constexpr size_t kNewsMaxPayloadLen = 4 + kNewsIdMaxLen + 1 + 10 + 1 + kNewsSourceMaxLen + 1 + kNewsTitleMaxLen + 1 + 64 + 2;

// Un messaggio e' accettato solo se il suo ts e' entro queste finestre
// rispetto all'ora della BBS (controllo fatto solo con orologio valido).
constexpr uint32_t kNewsMaxAgeSecs = 72u * 3600u;
constexpr uint32_t kNewsMaxFutureSecs = 3600u;

// Ultimi id ricordati per scartare i doppioni (MQTT QoS 1 puo' consegnare
// piu' volte).
constexpr size_t kNewsSeenCapacity = 64;
constexpr size_t kNewsSeenHashLen = 8;

struct NewsSettings {
  bool enabled;
  char server[kNewsServerLen];
  uint16_t port;
  char username[kNewsUserLen];
  char password[kNewsPassLen];
  char topic[kNewsTopicLen];
  uint8_t key[kNewsKeyMaxLen];
  uint8_t key_len;  // 0 = non impostata, altrimenti 16-32
  uint8_t room_id;  // stanza delle news, kNoRoom = non impostata

  // Valori predefiniti: disattivato, nessun server, porta 1883, topic
  // "bbs/news", nessuna chiave, nessuna stanza.
  NewsSettings();
};

enum class NewsIngestResult : uint8_t {
  OK,
  NOT_ENABLED,  // (non DISABLED: e' una macro del core Arduino-ESP32)
  NO_KEY,
  NO_ROOM,
  BAD_FORMAT,
  BAD_SIGNATURE,
  DUPLICATE,
  STALE,
  IO_ERROR,
};

// Nome breve dell'esito, per i log seriali (es. "BAD_SIGNATURE").
const char* newsResultName(NewsIngestResult r);

// Stato solo RAM (non persistito) per il comando NEWS.
struct NewsStatus {
  bool connected = false;       // aggiornato dal firmware (client MQTT)
  uint32_t received_ok = 0;     // news accodate dall'avvio
  bool has_result = false;
  NewsIngestResult last_result = NewsIngestResult::OK;
  uint32_t last_ok_ts = 0;      // 0 = nessuna ancora
};

// Impostazioni persistite in un file a layout fisso e versionato, scritto
// campo per campo. File assente/corto/di versione ignota/con valori fuori
// range = default. Copia in RAM, aggiornata solo dopo una scrittura riuscita
// ("persisti-poi-applica", come DigestStore).
class NewsStore {
public:
  explicit NewsStore(IFileSystem& fs, const char* path = "/bbs/news.dat");

  const NewsSettings& get() const { return _settings; }
  bool save(const NewsSettings& s);

  // Incrementato a ogni salvataggio riuscito: il client MQTT lo usa per
  // sapere quando rileggere la configurazione e riconnettersi.
  uint32_t generation() const { return _generation; }

  const NewsStatus& status() const { return _status; }
  void setConnected(bool connected) { _status.connected = connected; }
  void recordResult(NewsIngestResult r, uint32_t now_ts);

private:
  IFileSystem& _fs;
  const char* _path;
  NewsSettings _settings;
  uint32_t _generation = 0;
  NewsStatus _status;

  void load();
};

// Anello persistito degli ultimi kNewsSeenCapacity id gia' visti (di ognuno
// si conservano solo 8 byte dello SHA-256: un falso doppione e' trascurabile).
class NewsSeen {
public:
  explicit NewsSeen(IFileSystem& fs, const char* path = "/bbs/news_seen.dat");

  bool contains(const char* id) const;
  // Aggiunge l'id (no-op se gia' presente). L'anello in RAM viene sempre
  // aggiornato, cosi' il doppione resta riconosciuto anche se la scrittura
  // fallisce; ritorna false se la scrittura su file e' fallita.
  bool add(const char* id);

private:
  IFileSystem& _fs;
  const char* _path;
  uint8_t _hashes[kNewsSeenCapacity][kNewsSeenHashLen];
  uint8_t _count = 0;
  uint8_t _head = 0;  // prossimo slot da scrivere

  void load();
  bool persist() const;
  static void hashId(const char* id, uint8_t out[kNewsSeenHashLen]);
  bool containsHash(const uint8_t h[kNewsSeenHashLen]) const;
};

struct NewsMessage {
  char id[kNewsIdMaxLen + 1];
  uint32_t ts;
  char source[kNewsSourceMaxLen + 1];
  char title[kNewsTitleMaxLen + 1];
  uint8_t hmac[32];
};

// Validazione rigorosa del payload (vedi sopra). false se non conforme; in
// quel caso 'out' non e' significativo.
bool parseNewsMessage(const char* payload, size_t len, NewsMessage& out);

// true se l'HMAC del messaggio e' quello atteso con la chiave data.
bool verifyNews(const NewsMessage& msg, const uint8_t* key, size_t key_len);

// Elabora un payload MQTT: in ordine DISABLED, NO_KEY, NO_ROOM (stanza non
// impostata o non nel registro), BAD_FORMAT, BAD_SIGNATURE, STALE (solo se
// 'clock_valid': ts piu' vecchio di 72 ore o a piu' di 1 ora nel futuro),
// DUPLICATE. Se tutto va bene accoda "[<fonte>] <titolo>" (troncato su un
// confine UTF-8 a BBS_MAX_TEXT_LEN) alla stanza, a nome di 'system_user', con
// timestamp 'now_ts' (NON il ts dello script: i log devono restare in ordine
// cronologico), applica la ritenzione e ricorda l'id. Ignora lo stato
// "chiusa" della stanza (CLOSE serve a impedire che gli utenti vi scrivano).
// Aggiorna lo stato RAM di 'news' (ultimo esito, conteggio, ultima ricezione).
NewsIngestResult ingestNews(NewsStore& news, NewsSeen& seen, PostStore& posts, RoomRegistry& rooms,
                            UserStore& users, UserId system_user, const char* payload, size_t len,
                            uint32_t now_ts, bool clock_valid);

} // namespace bbs
