#pragma once

#include "bbs_config.h"
#include "bbs_port.h"
#include "bbs_types.h"

namespace bbs {

// Log dei post per stanza: un file append-only per stanza
// (/bbs/r<NN>.log), record a lunghezza variabile con intestazione fissa e
// CRC16 finale. I post sono immutabili: non c'e' riscrittura in-place se
// non nel percorso di riparazione (vedi repairRoom).
class PostStore {
public:
  explicit PostStore(IFileSystem& fs);

  // Aggiunge un post in coda al log della stanza. Va chiamato solo dopo
  // repairRoom() per quella stanza in questo avvio (altrimenti un record
  // troncato da uno spegnimento improvviso resterebbe in mezzo al file).
  bool appendPost(uint8_t room_id, UserId author, uint32_t timestamp, const char* text);

  // Cerca il primo post nella stanza con timestamp > after_ts (i post sono
  // in ordine cronologico crescente per costruzione). Ritorna false se non
  // ce ne sono.
  bool findNextUnread(uint8_t room_id, uint32_t after_ts, PostRecord& out) const;

  // Conta i post nella stanza con timestamp > after_ts (scan completo,
  // pensato per essere chiamato solo al LOGIN, non nel percorso caldo).
  uint32_t countUnread(uint8_t room_id, uint32_t after_ts) const;

  // Ripara il log della stanza: se l'ultimo record e' troncato o ha un CRC
  // non valido, il file viene riscritto fino all'ultimo record valido. Da
  // chiamare una volta per stanza all'avvio, prima di qualunque appendPost.
  // Non fa nulla (e ritorna true) se il file non esiste ancora.
  bool repairRoom(uint8_t room_id);

  // Moderazione (Fase 3): segna come cancellato l'ultimo post della stanza
  // (i post sono immutabili, non si cancella nulla fisicamente: si marca un
  // bit in flags, che findNextUnread/countUnread rispettano). Ritorna false
  // se la stanza non ha post.
  bool deleteLastPost(uint8_t room_id);

  // Ritenzione (Fase 4): se la stanza ha piu' di 'max_records' post, scarta
  // definitivamente i piu' vecchi (a differenza di deleteLastPost, qui il
  // dato e' perso per sempre, non solo nascosto). No-op se gia' sotto il
  // limite.
  bool enforceRetention(uint8_t room_id, uint32_t max_records);

  // Ricerca semplice (Fase 4): cerca all'indietro, tra gli ultimi
  // 'max_scan' post della stanza (i piu' recenti), il piu' recente che
  // contiene 'needle' come sottostringa esatta (case-sensitive). Ritorna
  // false se non trovato.
  bool searchRecent(uint8_t room_id, const char* needle, uint32_t max_scan, PostRecord& out) const;

  // Stanze dinamiche (Fase 5): rimuove il log della stanza (se esiste). Da
  // chiamare quando una stanza viene cancellata dal registro (vedi
  // bbs_room_registry.h), cosi' un id riassegnato a una stanza futura non
  // ne eredita i post. Ritorna true anche se il file non esisteva.
  bool purgeRoom(uint8_t room_id);

  // Post fissati (Fase 6): al piu' un post fissato per stanza, come un
  // bollettino che resta in cima finche' un moderatore non lo rimuove o non
  // ne fissa un altro. Fissa l'ultimo post della stanza (stessa convenzione
  // di deleteLastPost: nessuna numerazione dei post esposta agli utenti).
  // Ritorna false se la stanza non ha post.
  bool pinLastPost(uint8_t room_id);

  // Rimuove il fissaggio corrente della stanza, se presente. Ritorna false
  // se non c'era nessun post fissato (o la stanza non ha nemmeno un log).
  bool unpinRoom(uint8_t room_id);

  // Il post attualmente fissato nella stanza, se c'e' (e non e' stato nel
  // frattempo cancellato con DELPOST). Ritorna false altrimenti.
  bool findPinned(uint8_t room_id, PostRecord& out) const;

private:
  IFileSystem& _fs;

  static void roomPath(uint8_t room_id, char* out, size_t cap);
  bool readRecord(IFile& f, PostRecord& out) const;
  bool writeRecord(IFile& f, const PostRecord& rec) const;
};

} // namespace bbs
