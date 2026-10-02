#pragma once

#include <cstddef>
#include <cstdint>

#include <FS.h>
#include <MeshCore.h>

#include "../bbs/bbs_port.h"

// Punto d'ingresso unico usato da examples/bbs_room_server/MyMesh.cpp:
// nessun altro file del firmware deve includere direttamente gli header di
// lib/bbs/ o lib/bbs_port/.

namespace bbs {
namespace port {

// Da chiamare una volta in MyMesh::begin(), dopo il mount del filesystem
// (LittleFS.begin(...)) e prima di ricevere qualunque messaggio. Chiamate
// successive non hanno effetto. 'reply_channel' e' usato dal Notifier (Fase
// 2) per le notifiche asincrone di post nuovi — vedi bbs_port_reply_channel.h.
void init(fs::FS& filesystem, mesh::RTCClock* clock, IReplyChannel* reply_channel);

// Elabora un comando BBS da un client MeshCore gia' autenticato (pubkey
// nota tramite l'handshake ANON_REQ esistente). Scrive la risposta
// null-terminata in 'out' (capacita' out_cap) e ne ritorna la lunghezza
// (0 se non c'e' risposta o se init() non e' ancora stato chiamato).
size_t handleClientMessage(const uint8_t pub_key[32], const char* input, char* out, size_t out_cap);

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
