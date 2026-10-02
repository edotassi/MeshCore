#pragma once

#include <cstddef>

#include "bbs_config.h"
#include "bbs_port.h"
#include "bbs_result.h"
#include "bbs_types.h"

namespace bbs {

// Tabella utenti BBS: record a dimensione fissa (BBS_USER_RECORD_SIZE byte),
// append-only, accesso diretto per user_id via seek(id * RECORD_SIZE).
// Volutamente separata da src/helpers/ClientACL.h (condiviso con altri
// target del firmware): nessuna modifica a quel file.
class UserStore {
public:
  explicit UserStore(IFileSystem& fs, const char* path = "/bbs/users.dat");

  // kInvalidUserId se la chiave non e' registrata.
  UserId findByPubkey(const uint8_t pub_key[BBS_PUBKEY_LEN]) const;

  // kInvalidUserId se nessun utente ha esattamente questo nickname
  // (confronto case-sensitive, come al momento della registrazione).
  UserId findByNickname(const char* nickname) const;

  RegisterResult registerUser(const uint8_t pub_key[BBS_PUBKEY_LEN], const char* nickname,
                              uint32_t now_ts, UserId& out_id);

  bool touchLogin(UserId id, uint32_t now_ts);

  // Puntatore "ultimo letto" per stanza (0 se l'utente non ha mai letto
  // nulla in quella stanza, o se id/room_id non sono validi).
  uint32_t getLastRead(UserId id, uint8_t room_id) const;
  bool setLastRead(UserId id, uint8_t room_id, uint32_t timestamp);

  // Puntatore "ultima mail letta" (cursore unico, non per stanza).
  uint32_t getLastMailRead(UserId id) const;
  bool setLastMailRead(UserId id, uint32_t timestamp);

  // Iscrizione alle stanze (persistita nel record utente). Di default, alla
  // registrazione, solo le stanze principali (bbs_room_registry.h).
  bool isSubscribed(UserId id, uint8_t room_id) const;
  bool setSubscribed(UserId id, uint8_t room_id, bool subscribed);

  // Stanze dinamiche (Fase 5): azzera per TUTTI gli utenti il bit di
  // iscrizione e il puntatore last_read di room_id. Da chiamare quando una
  // stanza viene cancellata dal registro, cosi' un id riassegnato a una
  // stanza futura non eredita iscrizioni/puntatori della stanza precedente.
  bool clearRoomForAllUsers(uint8_t room_id);

  // Ruoli (Fase 3). Il primo utente mai registrato in questo archivio
  // diventa automaticamente ROLE_ADMIN (bootstrap: altrimenti nessuno
  // potrebbe mai promuovere il primo admin); tutti i successivi sono
  // ROLE_USER finche' un admin non li promuove.
  UserRole getRole(UserId id) const;
  bool setRole(UserId id, UserRole role);

  // Moderazione (Fase 3): bit in UserRecord::flags. Un utente bannato non
  // riceve piu' risposte ne' notifiche (vedi bbs_command_parser.cpp); uno
  // silenziato puo' ancora leggere ma non pubblicare post ne' mail.
  bool isBanned(UserId id) const;
  bool setBanned(UserId id, bool banned);
  bool isMuted(UserId id) const;
  bool setMuted(UserId id, bool muted);

  // Copia il nickname (incl. terminatore) in out_buf (almeno BBS_NICK_LEN byte).
  bool getNickname(UserId id, char* out_buf, size_t out_cap) const;

  // Copia la chiave pubblica (BBS_PUBKEY_LEN byte) in out_buf.
  bool getPubkey(UserId id, uint8_t out_buf[BBS_PUBKEY_LEN]) const;

  UserId count() const;

  static bool isValidNickname(const char* nickname);

private:
  IFileSystem& _fs;
  const char* _path;

  bool readRecordAt(UserId id, UserRecord& out) const;
  bool readFieldsFromFile(IFile& f, UserRecord& rec) const;
  bool writeFieldsToFile(IFile& f, const UserRecord& rec) const;

  // Riscrive l'intero file su un temporaneo applicando 'mutate' (con i due
  // argomenti dati) al record di indice 'id', poi rinomina il temporaneo
  // sopra l'originale. Usato da touchLogin/setLastRead: mai apertura
  // contemporanea in lettura+scrittura sullo stesso file.
  bool rewriteApplying(UserId id, void (*mutate)(UserRecord&, uint32_t, uint32_t), uint32_t arg1,
                       uint32_t arg2);
};

} // namespace bbs
