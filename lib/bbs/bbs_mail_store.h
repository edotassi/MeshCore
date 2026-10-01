#pragma once

#include "bbs_config.h"
#include "bbs_port.h"
#include "bbs_types.h"

namespace bbs {

// Mail privata tra utenti registrati: un unico log append-only
// (/bbs/mail.log) condiviso da tutti, con lo stesso formato/robustezza del
// log dei post (bbs_post_store.h). Un volume di mail modesto per una
// piccola comunita' mesh rende accettabile lo scan lineare per
// destinatario, evitando di gestire centinaia di file (uno per utente).
class MailStore {
public:
  explicit MailStore(IFileSystem& fs, const char* path = "/bbs/mail.log");

  bool send(UserId sender, UserId recipient, uint32_t timestamp, const char* text);

  // Cerca la prima mail per 'recipient' con timestamp > after_ts.
  bool findNextUnread(UserId recipient, uint32_t after_ts, MailRecord& out) const;

  // Conta le mail per 'recipient' con timestamp > after_ts (scan completo,
  // pensato per essere chiamato solo al LOGIN).
  uint32_t countUnread(UserId recipient, uint32_t after_ts) const;

  // Numero totale di mail nel log condiviso, qualunque destinatario
  // (Fase 4, STATS).
  uint32_t totalCount() const;

  // Ripara il log (coda troncata/CRC invalido dopo uno spegnimento
  // improvviso), come PostStore::repairRoom. Da chiamare una volta
  // all'avvio, prima di qualunque send().
  bool repair();

private:
  IFileSystem& _fs;
  const char* _path;

  bool readRecord(IFile& f, MailRecord& out) const;
  bool writeRecord(IFile& f, const MailRecord& rec) const;
};

} // namespace bbs
