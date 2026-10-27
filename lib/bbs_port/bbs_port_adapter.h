#pragma once

#include <cstddef>
#include <cstdint>

#include <FS.h>
#include <Mesh.h>
#include <MeshCore.h>

#include "../bbs/bbs_port.h"

// Punto d'ingresso unico usato da examples/bbs_room_server/MyMesh.cpp:
// nessun altro file del firmware deve includere direttamente gli header di
// lib/bbs/ o lib/bbs_port/.

namespace bbs {
namespace port {

// Da chiamare una volta in MyMesh::begin(), dopo il mount del filesystem
// (LittleFS.begin(...)) e prima di ricevere qualunque messaggio. Chiamate
// successive non hanno effetto. 'reply_channel' e' usato dal Notifier per
// il push asincrono dei post — vedi bbs_port_reply_channel.h.
void init(fs::FS& filesystem, mesh::RTCClock* clock, IReplyChannel* reply_channel);

// Da chiamare in MyMesh::begin() dopo init() e dopo che l'identita' del nodo
// e' stata caricata: registra (se manca) l'utente di sistema "News", con la
// chiave pubblica del nodo, che firma i post delle news da MQTT. Va chiamata
// prima che qualunque utente si registri (altrimenti il bootstrap del primo
// admin e' comunque corretto: l'utente di sistema non conta). No-op se init()
// non e' stato chiamato.
void setNodeIdentity(const uint8_t pub_key[32]);

// Elabora un comando BBS da un client MeshCore gia' autenticato (pubkey
// nota tramite l'handshake ANON_REQ esistente). Scrive la risposta
// null-terminata in 'out' (capacita' out_cap) e ne ritorna la lunghezza
// (0 se non c'e' risposta o se init() non e' ancora stato chiamato).
size_t handleClientMessage(const uint8_t pub_key[32], const char* input, char* out, size_t out_cap);

// Chiamata periodicamente da MyMesh::loop() (ogni ~30 secondi). Ritorna true
// se va inviato ora il riepilogo giornaliero (o uno richiesto con DIGEST
// NOW): il testo, SENZA prefisso mittente (al massimo text_cap-1 byte), e'
// in 'text'; la PSK del canale di gruppo e' in 'psk' (buffer da 32 byte,
// azzerato oltre 'psk_len', che vale 16 o 32). Ritorna false se non c'e'
// nulla da inviare, se il giorno viene solo segnato come fatto (nessun post,
// o finestra di recupero scaduta) o se init() non e' ancora stato chiamato.
bool pollDigest(char* text, size_t text_cap, uint8_t psk[32], size_t& psk_len);

// --- Test bot (vedi bbs_bot.h) ---

// Per MyMesh::searchChannelsByHash: copia in 'out' (al piu' 'max') i canali
// del bot il cui hash di 1 byte coincide con 'hash', con la chiave gia'
// risolta (anche per i canali hashtag). Ritorna 0 se il bot e' disattivato
// o init() non e' ancora stato chiamato.
int botSearchChannels(const uint8_t* hash, mesh::GroupChannel out[], int max);

// Indice (nella tabella dei canali del bot) del canale con questa chiave, -1
// se non e' un canale del bot.
int botChannelIndex(const mesh::GroupChannel& ch);

// Per MyMesh::onGroupDataRecv: 'text' e' il testo del messaggio di gruppo
// ("<mittente>: <corpo>", null-terminato). Se e' un "test"/"!test" e il
// cooldown del canale 'channel_idx' (da botChannelIndex) lo consente,
// scrive in 'out' la risposta, SENZA prefisso del nodo, e ne ritorna la
// lunghezza. Ritorna 0 (nulla da inviare) se non e' un comando, se il bot e'
// disattivato, se l'indice non e' valido o se il cooldown e' attivo.
// 'path' contiene 'hop_count' hash di 'hash_size' byte ciascuno. Il controllo
// "messaggio mio" spetta al chiamante.
size_t botHandleGroupText(int channel_idx, const char* text, uint8_t hop_count, const uint8_t* path,
                          uint8_t hash_size, unsigned long now_ms, char* out, size_t out_cap);

// --- News da MQTT (vedi bbs_news.h) ---

// Configurazione per il client MQTT del firmware (BbsNewsClient).
struct NewsClientConfig {
  bool enabled;
  char server[64];
  uint16_t port;
  char username[32];
  char password[32];
  char topic[64];
};

// Copia la configurazione corrente in 'out' e in 'generation' un contatore
// che cambia a ogni modifica (comando NEWS): il client si riconnette quando
// cambia. Ritorna false (out non toccato) se init() non e' ancora stato
// chiamato.
bool newsGetConfig(NewsClientConfig& out, uint32_t& generation);

// Il client MQTT segnala se e' connesso e iscritto al topic (per il comando NEWS).
void newsSetConnected(bool connected);

// Elabora un payload MQTT ricevuto (vedi bbs_news.h per il formato) e lo
// accoda alla stanza delle news. Ritorna il codice di esito
// (bbs::NewsIngestResult come intero, 0 = accodata), -1 se init() non e' ancora stato
// chiamato. 'name' (se non nullptr) riceve il nome testuale dell'esito.
int newsIngest(const uint8_t* payload, size_t len, const char** name = nullptr);

// Dump testuale (stanze, utenti, post) su Serial, per un backup leggibile
// prima di un repair/riflash, pensato per essere riletto da importFeedLine()
// (formato "BBS EXPORT v1", vedi bbs_port_adapter.cpp). Da chiamare SOLO da
// un comando seriale (mai raggiungibile dalla mesh LoRa): non invia nulla
// via radio. Non include il contenuto della mail privata (dato personale,
// non necessario per un backup dei contenuti pubblici della BBS), ne'
// iscrizioni/puntatori di lettura (stato poco interessante da conservare:
// dopo un ripristino, tutti ripartono iscritti ai default con tutto da
// leggere). created_ts/last_login_ts non sono preservati: dopo un
// ripristino valgono il momento dell'import, non l'originale. No-op se
// init() non e' ancora stato chiamato.
void exportToSerial();

// Avvia un import: ripristina stanze/utenti/post da un testo nel formato
// prodotto da exportToSerial(), incollato riga per riga nella console
// seriale dopo questa chiamata. Rifiutato (ritorna false, nessuno stato
// cambiato) se il nodo ha gia' almeno un utente registrato — l'import e'
// pensato solo per un nodo appena flashato/cancellato, non per un merge con
// dati esistenti. Da chiamare SOLO da un comando seriale.
bool importStart();

// true se un import e' in corso (avviato con importStart() e non ancora
// concluso dalla riga "=== FINE EXPORT ==="). Mentre e' vero,
// MyMesh::handleCommand deve passare OGNI riga ricevuta da seriale a
// importFeedLine() invece di interpretarla come un comando — stesso schema
// gia' in uso in questo firmware per `region_load_active`/CommonCLI.
bool importInProgress();

// Elabora una riga del testo incollato durante un import. Errori di
// formato/dati (riga malformata, pubkey non valida, autore non trovato per
// un post) sono stampati su Serial e saltano solo quella riga, senza
// interrompere l'import: alla fine ("=== FINE EXPORT ===") viene stampato
// un riepilogo se ci sono stati errori. Non ha effetto se nessun import e'
// in corso.
void importFeedLine(const char* line);

} // namespace port
} // namespace bbs
