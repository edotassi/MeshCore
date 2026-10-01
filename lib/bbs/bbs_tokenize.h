#pragma once

#include <cstddef>

namespace bbs {

// Estrae il primo token grezzo da 'input' (nessuna conversione di
// maiuscole/minuscole) in 'token_out', troncato a token_cap-1 caratteri.
// Ritorna il puntatore al resto della stringa, con gli spazi iniziali (sia
// prima del token che dopo) gia' saltati. 'input' non viene modificato.
// Usato per estrarre argomenti case-sensitive come i nickname.
const char* extractToken(const char* input, char* token_out, size_t token_cap);

// Come extractToken, ma il token estratto viene convertito in maiuscolo:
// usato per i nomi dei comandi (REGISTER, LOGIN, ...).
const char* extractCommand(const char* input, char* cmd_out, size_t cmd_cap);

} // namespace bbs
