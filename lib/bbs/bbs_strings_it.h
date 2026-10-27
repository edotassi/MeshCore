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
    "Comandi: scrivi per postare, E idem, N rileggi, S/U entra/esci, "
    "K stanze, M mail, H aiuto, LOGIN, LOGOUT.";

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

constexpr const char* kPostEmptyText = "Testo vuoto. Scrivi il messaggio da pubblicare.";

constexpr const char* kNoRoomSelected = "Non sei in nessuna stanza. Usa: S <stanza>.";

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

// Concatenato dal parser con il nome della stanza (S).
constexpr const char* kJoinedRoomPrefix = "Sei in: ";
// Prefisso del post fissato mostrato subito dopo l'ingresso in una stanza
// (S), prima di "<nickname>: <testo>".
constexpr const char* kPinnedInline = "Fissato: ";
// Concatenato dal parser con il nome della stanza (U).
constexpr const char* kLeftRoomPrefix = "Uscito da: ";
constexpr const char* kNotInAnyRoom = "Non eri in nessuna stanza.";

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
constexpr const char* kSearchEmptyKeyword = "Usa: SEARCH <parola>.";
constexpr const char* kSearchNoMatch = "Nessun risultato tra i post recenti.";
constexpr const char* kMotdNotSet = "Nessun messaggio del giorno impostato.";
constexpr const char* kMotdCleared = "Messaggio del giorno rimosso.";
constexpr const char* kMotdSetOk = "Messaggio del giorno aggiornato.";

// Format string per snprintf: statistiche del nodo.
constexpr const char* kStatsFmt = "%u utenti, %u online, %u post, %u mail.";

// Fase 5: stanze dinamiche (solo amministratore).
constexpr const char* kRoomUsage = "Usa: ROOM ADD <nome> oppure ROOM DEL <stanza>.";
constexpr const char* kRoomNameInvalid =
    "Nome stanza non valido: usa 1-15 caratteri (lettere, numeri, - oppure _).";
constexpr const char* kRoomNameDuplicate = "Esiste gia' una stanza con questo nome.";
constexpr const char* kRoomsFull = "Limite di stanze raggiunto.";
constexpr const char* kRoomAddFailed = "Impossibile creare la stanza (errore di scrittura).";
constexpr const char* kRoomDelFailed = "Impossibile cancellare la stanza (errore di scrittura).";

// Concatenati dal parser con il nome della stanza.
constexpr const char* kRoomAddOkPrefix = "Stanza creata: ";
constexpr const char* kRoomDelOkPrefix = "Stanza cancellata: ";

// Fase 6: post fissati (solo moderatore/amministratore per PIN/UNPIN).
constexpr const char* kPinNothingToPin = "Nessun post da fissare in questa stanza.";
constexpr const char* kUnpinNothingToUnpin = "Nessun post fissato da rimuovere in questa stanza.";

// Concatenati dal parser con il nome della stanza.
constexpr const char* kPinOkPrefix = "Post fissato in ";
constexpr const char* kUnpinOkPrefix = "Fissaggio rimosso in ";
constexpr const char* kNoPinnedPrefix = "Nessun post fissato in ";

// Concatenato dal parser con "<nickname>: <testo>" del post fissato.

// Riepilogo giornaliero (DIGEST, solo amministratore).
// Format string per snprintf: %u = numero totale di post nelle ultime 24h.
constexpr const char* kDigestHeadFmt = "Ultime 24h: %u post. ";
constexpr const char* kDigestNoPosts = "Ultime 24h: nessun nuovo post.";

// Format string per snprintf dello stato: attivo/disattivato, ora, fuso
// (segno, ore, minuti), ora legale, canale, data dell'ultimo invio.
constexpr const char* kDigestStatusFmt =
    "Riepilogo: %s, ore %02u:%02u, fuso %c%u:%02u %s, canale %s, ultimo invio %s.";
constexpr const char* kDigestStatusOn = "attivo";
constexpr const char* kDigestStatusOff = "disattivato";
constexpr const char* kDigestDstEu = "con ora legale UE";
constexpr const char* kDigestDstNone = "senza ora legale";
constexpr const char* kDigestChannelPublic = "Public";
constexpr const char* kDigestChannelPrivate = "privato";
constexpr const char* kDigestNever = "mai";
// Format string: anno, mese, giorno.
constexpr const char* kDigestDateFmt = "%04d-%02u-%02u";

constexpr const char* kDigestUsage =
    "Usa: DIGEST [ON|OFF|TIME HH:MM|TZ +H[:MM]|DST ON|OFF|CHANNEL PUBLIC|<psk>|NOW].";
constexpr const char* kDigestBadTime = "Ora non valida. Usa: DIGEST TIME HH:MM (00:00-23:59).";
constexpr const char* kDigestBadTz = "Fuso non valido. Usa: DIGEST TZ +H[:MM], da -12:00 a +14:00.";
constexpr const char* kDigestBadPsk = "Chiave non valida: serve una PSK di 16 o 32 byte (esadecimale o base64).";
constexpr const char* kDigestSaveFailed = "Impossibile salvare il riepilogo (errore di scrittura).";
constexpr const char* kDigestOnOk = "Riepilogo giornaliero attivato.";
constexpr const char* kDigestOffOk = "Riepilogo giornaliero disattivato.";
constexpr const char* kDigestTimeOkFmt = "Riepilogo programmato alle %02u:%02u.";
constexpr const char* kDigestTzOkFmt = "Fuso impostato: %c%u:%02u.";
constexpr const char* kDigestDstOnOk = "Ora legale UE attivata.";
constexpr const char* kDigestDstOffOk = "Ora legale UE disattivata.";
constexpr const char* kDigestChannelPublicOk = "Canale impostato: Public.";
constexpr const char* kDigestChannelPrivateOk = "Canale privato impostato.";
constexpr const char* kDigestNowOk = "Riepilogo in invio a breve.";

// Test bot (BOT, solo amministratore).
// Format string per snprintf dello stato: attivo/disattivato, elenco canali.
constexpr const char* kBotStatusFmt = "Bot: %s, canali: %s.";
constexpr const char* kBotStatusOn = "attivo";
constexpr const char* kBotStatusOff = "disattivato";
constexpr const char* kBotNoChannels = "nessuno";
constexpr const char* kBotUsage = "Usa: BOT [ON|OFF|CHANNEL ADD #nome|PUBLIC|<nome> <psk>|CHANNEL DEL <nome>].";
constexpr const char* kBotOnOk = "Bot attivato.";
constexpr const char* kBotOffOk = "Bot disattivato.";
// Format string: nome del canale.
constexpr const char* kBotChannelAddedFmt = "Canale aggiunto: %s.";
constexpr const char* kBotChannelRemovedFmt = "Canale rimosso: %s.";
constexpr const char* kBotChannelFull = "Troppi canali: il massimo e' 4.";
constexpr const char* kBotChannelDuplicate = "Canale gia' presente.";
constexpr const char* kBotChannelNotFound = "Canale non trovato.";
constexpr const char* kBotBadName =
    "Nome non valido: usa #nome (a-z, 0-9, -, max 22) oppure un nome di 1-23 caratteri senza spazi.";
constexpr const char* kBotSaveFailed = "Impossibile salvare il bot (errore di scrittura).";

// Risposta del test bot, dopo "@[<mittente>": senza hop, oppure (format
// string per snprintf) con %u = numero di hop, seguito dall'elenco del percorso.
constexpr const char* kBotReplyDirect = " ricevuto diretto (0 hop)";
constexpr const char* kBotReplyHopsFmt = "] ricevuto con %u hop via ";
// Stesse forme per la risposta a "ping".
constexpr const char* kBotPongDirect = " pong, diretto (0 hop)";
constexpr const char* kBotPongHopsFmt = "] pong, %u hop via ";

// Risposta di "!news" quando non c'e' nessuna news (dopo "@[<mittente>] ").
constexpr const char* kBotNewsNone = "nessuna news disponibile";

// News da MQTT (NEWS, solo amministratore).
// Format string per snprintf dello stato: attive/disattivate,
// canale di trasmissione, stanza, chiave impostata, connessione, news
// ricevute, server, porta, topic. Server e topic (i campi piu' lunghi) stanno
// in fondo: se la riga supera il limite si accorciano loro.
constexpr const char* kNewsStatusFmt = "News %s, canale %s, stanza %s, chiave %s, %s, ric. %u, %s:%u, topic %s.";
constexpr const char* kNewsNoChannel = "nessuno";
constexpr const char* kNewsStatusOn = "attive";
constexpr const char* kNewsStatusOff = "disattivate";
constexpr const char* kNewsNotSet = "-";
constexpr const char* kNewsYes = "si";
constexpr const char* kNewsNo = "no";
constexpr const char* kNewsConnected = "connesso";
constexpr const char* kNewsNotConnected = "non connesso";

constexpr const char* kNewsUsage =
    "Usa: NEWS [ON|OFF|SERVER <host> [porta]|USER <utente> <password>|USER -|TOPIC <topic>|KEY <base64>|ROOM <n>|CHANNEL OFF|#nome|<nome> <psk>].";
constexpr const char* kNewsOnOk = "News attivate.";
constexpr const char* kNewsOffOk = "News disattivate.";
constexpr const char* kNewsBadServer = "Server non valido: usa un nome host (max 63 caratteri) e una porta 1-65535.";
constexpr const char* kNewsBadUser = "Utente o password non validi: max 31 caratteri ciascuno, senza spazi.";
constexpr const char* kNewsBadTopic = "Topic non valido: 1-63 caratteri senza spazi.";
constexpr const char* kNewsBadKey = "Chiave non valida: serve una chiave di 16-32 byte (esadecimale o base64).";
constexpr const char* kNewsSaveFailed = "Impossibile salvare le news (errore di scrittura).";
// Format string per snprintf: host, porta.
constexpr const char* kNewsServerOkFmt = "Server news impostato: %s:%u.";
constexpr const char* kNewsUserOk = "Credenziali news impostate.";
constexpr const char* kNewsUserCleared = "Credenziali news rimosse.";
// Concatenato dal parser con il topic.
constexpr const char* kNewsTopicOkPrefix = "Topic news impostato: ";
constexpr const char* kNewsKeyOk = "Chiave news impostata.";
// Concatenato dal parser con il nome della stanza.
constexpr const char* kNewsRoomOkPrefix = "Stanza news impostata: ";
constexpr const char* kNewsChannelUsage = "Usa: NEWS CHANNEL OFF|#nome|<nome> <psk>.";
constexpr const char* kNewsChannelOff = "Trasmissione news sul canale disattivata.";
// Format string: nome del canale.
constexpr const char* kNewsChannelOkFmt = "Le news saranno trasmesse sul canale %s (una ogni 30 s).";

} // namespace strings
} // namespace bbs
