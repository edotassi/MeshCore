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

} // namespace port
} // namespace bbs
