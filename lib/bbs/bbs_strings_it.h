#pragma once

// Testi in italiano per il nucleo BBS (identita', benvenuto, aiuto).
// Scritti senza lettere accentate per restare ben dentro il budget di
// bbs::BBS_MAX_TEXT_LEN byte per pacchetto (una accentata in UTF-8 costa
// 2 byte) e per evitare sorprese nella resa su OLED/client — vedi
// UPSTREAM.md per la decisione. Nessun testo utente va scritto altrove:
// il parser comandi (bbs_command_parser.cpp) compone le risposte solo
// concatenando queste costanti con dati (es. il nickname).

namespace bbs {
namespace strings {

constexpr const char* kWelcome =
    "Benvenuto sulla BBS! Per registrarti scrivi: REGISTER <nome>. "
    "Poi usa H per l'elenco comandi.";

constexpr const char* kUnknownPreRegister =
    "Comando sconosciuto. Scrivi REGISTER <nome> per registrarti, o H per aiuto.";

constexpr const char* kHelpPreRegister =
    "Comandi disponibili: REGISTER <nome> (registrati), LOGIN, H (aiuto).";

constexpr const char* kHelpPostRegister =
    "Comandi: K stanze, N nuovi, E scrivi, M mail, S/U iscrivi/disiscrivi, "
    "H aiuto, LOGIN, LOGOUT.";

constexpr const char* kLoginNoAccount =
    "Nessun account con questa chiave. Registrati con: REGISTER <nome>";

// Format string per snprintf: %u nuovi messaggi, %u mail non lette.
constexpr const char* kLoginWelcomeBackFmt = "Bentornato! %u nuovi, %u mail. H per aiuto.";

constexpr const char* kLogoutAck = "Sessione terminata.";

// Concatenati dal parser con il nickname, senza separatore aggiuntivo.
constexpr const char* kAlreadyRegisteredPrefix = "Sei gia' registrato come ";
constexpr const char* kRegisterOkPrefix = "Registrazione completata. Benvenuto, ";

constexpr const char* kInvalidNickname =
    "Nome non valido: usa 1-15 caratteri (lettere, numeri, - oppure _).";

constexpr const char* kUnknownPostRegister =
    "Comando non riconosciuto. Scrivi H per l'elenco comandi.";

constexpr const char* kStoreFull =
    "Registrazioni esaurite al momento, riprova piu' tardi.";

constexpr const char* kInvalidRoom = "Stanza non valida. Usa K per l'elenco.";

constexpr const char* kPostEmptyText = "Testo vuoto. Usa: E <testo> oppure E <stanza> <testo>.";

// Concatenati dal parser con il nome della stanza.
constexpr const char* kPostOkPrefix = "Pubblicato in ";
constexpr const char* kNoNewPostsPrefix = "Nessun nuovo messaggio in ";

// Concatenati dal parser con "<nickname>: <testo>" del post trovato.

constexpr const char* kMailNoNew = "Nessuna mail nuova.";
constexpr const char* kMailUnknownRecipient = "Utente non trovato. Controlla il nome.";
constexpr const char* kMailEmptyText = "Testo vuoto. Usa: M <nome> <testo> per inviare, M per leggere.";

// Concatenato dal parser con il nickname del destinatario.
constexpr const char* kMailSentPrefix = "Mail inviata a ";

// Concatenato dal parser con "<nickname>: <testo>" della mail trovata.

// Concatenati dal parser con il nome della stanza.
constexpr const char* kSubscribedPrefix = "Iscritto a ";
constexpr const char* kUnsubscribedPrefix = "Disiscritto da ";

// Format string per snprintf: notifica accorpata di post nuovi (Fase 2).
// %u = conteggio, %s = nome stanza.
constexpr const char* kNewPostsNotifyFmt = "%u nuovi in %s, N per leggere";

// Fase 3: ruoli, moderazione, anti-abuso.
constexpr const char* kPermissionDenied = "Non hai i permessi per questo comando.";
constexpr const char* kTargetNotFound = "Utente non trovato. Controlla il nome.";
constexpr const char* kMutedCannotPost = "Sei silenziato: non puoi pubblicare ne' mandare mail.";
constexpr const char* kRoomClosed = "Stanza chiusa dai moderatori. Solo lettura.";
constexpr const char* kRateLimited = "Troppi messaggi, rallenta un attimo.";
constexpr const char* kDelPostNothingToDelete = "Nessun post da cancellare in questa stanza.";

// Concatenati dal parser con il nickname del bersaglio.
constexpr const char* kBanOkPrefix = "Utente bannato: ";
constexpr const char* kUnbanOkPrefix = "Utente sbannato: ";
constexpr const char* kMuteOkPrefix = "Utente silenziato: ";
constexpr const char* kUnmuteOkPrefix = "Utente riabilitato: ";
constexpr const char* kSetModOkPrefix = "Promosso a moderatore: ";
constexpr const char* kSetAdminOkPrefix = "Promosso ad admin: ";
constexpr const char* kSetUserOkPrefix = "Retrocesso a utente: ";

// Concatenato dal parser con il nome della stanza.
constexpr const char* kDelPostOkPrefix = "Ultimo post cancellato in ";
constexpr const char* kCloseOkPrefix = "Stanza chiusa: ";
constexpr const char* kOpenOkPrefix = "Stanza riaperta: ";

// Format string per snprintf: %u = numero di azioni nel registro.
constexpr const char* kModLogCountFmt = "%u azioni nel registro di moderazione.";

// Fase 4: funzioni leggere.
constexpr const char* kWhoNoOne = "Nessuno online al momento.";
constexpr const char* kSearchEmptyKeyword = "Usa: SEARCH <stanza> <parola>.";
constexpr const char* kSearchNoMatch = "Nessun risultato tra i post recenti.";
constexpr const char* kMotdNotSet = "Nessun messaggio del giorno impostato.";
constexpr const char* kMotdCleared = "Messaggio del giorno rimosso.";
constexpr const char* kMotdSetOk = "Messaggio del giorno aggiornato.";

// Format string per snprintf: statistiche del nodo.
constexpr const char* kStatsFmt = "%u utenti, %u online, %u post, %u mail.";

} // namespace strings
} // namespace bbs
