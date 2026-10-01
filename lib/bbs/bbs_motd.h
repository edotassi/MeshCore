#pragma once

#include <cstddef>

#include "bbs_config.h"
#include "bbs_port.h"

namespace bbs {

// Messaggio del giorno (Fase 4): un bollettino impostato dall'admin,
// mostrato al LOGIN al posto del conteggio dei non letti quando presente.
// File di testo grezzo, nessuna intestazione: la lunghezza e' la dimensione
// del file (assente/vuoto = nessun MOTD impostato).
class MotdStore {
public:
  explicit MotdStore(IFileSystem& fs, const char* path = "/bbs/motd.dat");

  // Copia il testo (null-terminated) in out_buf. Stringa vuota se non
  // impostato. Ritorna sempre true (un MOTD assente non e' un errore).
  bool get(char* out_buf, size_t out_cap) const;

  // Testo vuoto ("") cancella il MOTD.
  bool set(const char* text);

private:
  IFileSystem& _fs;
  const char* _path;
};

} // namespace bbs
