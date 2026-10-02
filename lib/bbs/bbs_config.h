#pragma once

#include <cstddef>
#include <cstdint>

namespace bbs {

// Numero massimo di stanze supportate (dimensiona il puntatore "ultimo letto"
// nel record utente). 8 e' ampio per una piccola comunita' mesh e mantiene il
// record utente compatto (4 byte in piu' per stanza).
constexpr uint8_t BBS_MAX_ROOMS = 8;

// Lunghezza del campo nickname, terminatore incluso.
constexpr size_t BBS_NICK_LEN = 16;
constexpr size_t BBS_NICK_MAX_CHARS = BBS_NICK_LEN - 1;

// Lunghezza del nome di una stanza, terminatore incluso (Fase 5, stanze
// dinamiche: vedi bbs_room_registry.h).
constexpr size_t BBS_ROOM_NAME_LEN = 16;
constexpr size_t BBS_ROOM_NAME_MAX_CHARS = BBS_ROOM_NAME_LEN - 1;

// Dimensione fissa di un record utente su file (vedi bbs_user_store.h).
constexpr size_t BBS_USER_RECORD_SIZE = 104;

// Limite di registrazioni: le comunita' mesh LoRa sono piccole (decine di
// nodi); 500 record x 104 byte ~= 51 KB, trascurabile sugli 8 MB della
// partizione dati.
constexpr uint32_t BBS_MAX_USERS = 500;

// Budget di testo utile per un pacchetto BBS: 160 byte (convenzione gia'
// usata nel codebase per MAX_POST_TEXT_LEN) meno 9 byte di framing
// applicativo (timestamp + tipo + prefisso pubkey), vedi UPSTREAM.md.
constexpr size_t BBS_MAX_TEXT_LEN = 151;

constexpr size_t BBS_PUBKEY_LEN = 32;

// Sessioni attive (RAM, Fase 2): numero massimo di utenti tracciati come
// "online adesso" contemporaneamente. Indipendente da BBS_MAX_USERS: gli
// account registrati possono essere molti di piu' di quanti sono attivi in
// un dato momento.
constexpr size_t BBS_MAX_SESSIONS = 32;

// Una sessione scade dopo questo periodo di inattivita' (in assenza di un
// LOGOUT esplicito). 40 minuti, nel mezzo dell'intervallo 30-60 suggerito
// dal piano originale.
constexpr uint32_t BBS_SESSION_TIMEOUT_SECS = 2400;

// Le notifiche di post nuovi si accorpano: un post in sospeso per una
// stanza aspetta questa finestra prima di essere inviato (nel frattempo
// altri post nella stessa stanza si accumulano nello stesso avviso).
constexpr uint32_t BBS_NOTIFY_BATCH_WINDOW_SECS = 120;

// Dopo questi mancati recapiti consecutivi, l'utente passa "offline" e non
// riceve piu' notifiche finche' non contatta di nuovo la BBS (qualunque
// comando azzera il contatore, vedi SessionTable::touch).
constexpr uint8_t BBS_MAX_MISSED_DELIVERIES = 3;

// Anti-abuso (Fase 3): messaggi (post E, mail M) consentiti per utente in
// una finestra di 60 secondi, oltre i quali si riceve un avviso breve
// invece di essere pubblicati/inviati.
constexpr uint8_t BBS_MAX_MESSAGES_PER_MINUTE = 6;

// Ritenzione (Fase 4): oltre questo numero di post per stanza, i piu'
// vecchi vengono scartati definitivamente (non solo nascosti come con
// deleteLastPost). Controllato dopo ogni E riuscito: uno scan completo
// della stanza per post e' accettabile dato il volume di traffico tipico
// di una mesh LoRa (molto piu' lento del tempo di lettura da flash anche
// per migliaia di record) — se in futuro una stanza si avvicinasse
// davvero a questo limite con alta frequenza, si puo' aggiungere un
// contatore in RAM per non ricontare ad ogni post, senza cambiare
// l'interfaccia pubblica.
constexpr uint32_t BBS_MAX_POSTS_PER_ROOM = 2000;

// Ricerca (Fase 4): quanti post recenti (i piu' nuovi) vengono scansionati
// da SEARCH, dal piu' recente all'indietro.
constexpr uint32_t BBS_SEARCH_MAX_SCAN = 200;

// Formato record del log dei post (vedi bbs_post_store.h): intestazione
// fissa (version, room_id, timestamp, user_id, text_len, flags) prima del
// testo a lunghezza variabile e del CRC16 finale.
constexpr size_t BBS_POST_HEADER_SIZE = 13;
constexpr uint8_t BBS_POST_RECORD_VERSION = 1;

// Formato record del log della mail privata (vedi bbs_mail_store.h):
// version, sender_id, recipient_id, timestamp, text_len, flags.
constexpr size_t BBS_MAIL_HEADER_SIZE = 16;
constexpr uint8_t BBS_MAIL_RECORD_VERSION = 1;

} // namespace bbs
