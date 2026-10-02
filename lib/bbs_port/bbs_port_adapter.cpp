#include "bbs_port_adapter.h"

#include <Arduino.h>

#include <cstring>

#include "../bbs/bbs_command_parser.h"
#include "../bbs/bbs_mail_store.h"
#include "../bbs/bbs_mod_log.h"
#include "../bbs/bbs_motd.h"
#include "../bbs/bbs_notifier.h"
#include "../bbs/bbs_pending_welcome.h"
#include "../bbs/bbs_post_store.h"
#include "../bbs/bbs_room_registry.h"
#include "../bbs/bbs_room_state.h"
#include "../bbs/bbs_session_table.h"
#include "../bbs/bbs_user_store.h"
#include "bbs_port_clock.h"
#include "bbs_port_littlefs.h"

namespace bbs {
namespace port {

namespace {

LittleFsFileSystem* g_fs = nullptr;
MeshCoreClock* g_clock = nullptr;
UserStore* g_users = nullptr;
PendingWelcomeTable* g_welcome = nullptr;
PostStore* g_posts = nullptr;
MailStore* g_mail = nullptr;
SessionTable* g_sessions = nullptr;
Notifier* g_notifier = nullptr;
RoomState* g_rooms = nullptr;
RoomRegistry* g_room_registry = nullptr;
ModLog* g_modlog = nullptr;
MotdStore* g_motd = nullptr;

} // namespace

void init(fs::FS& filesystem, mesh::RTCClock* clock, IReplyChannel* reply_channel) {
  if (g_fs != nullptr) return; // gia' inizializzato

  // LittleFS richiede che la directory esista prima di poter creare un
  // file al suo interno (a differenza di SPIFFS, che era senza vere
  // directory) — a differenza degli altri file scritti dal firmware,
  // che vivono nella radice.
  filesystem.mkdir("/bbs");

  // Uniche allocazioni dinamiche della BBS: una volta sola, qui, prima che
  // arrivi qualunque messaggio (stesso schema gia' in uso nel resto del
  // firmware per gli oggetti costruiti una tantum a inizializzazione,
  // es. StaticPoolPacketManager in MyMesh).
  g_fs = new LittleFsFileSystem(filesystem);
  g_clock = new MeshCoreClock(clock);
  g_users = new UserStore(*g_fs);
  g_welcome = new PendingWelcomeTable(*g_fs);
  g_posts = new PostStore(*g_fs);
  g_mail = new MailStore(*g_fs);
  g_sessions = new SessionTable();
  g_rooms = new RoomState(*g_fs);
  g_room_registry = new RoomRegistry(*g_fs);
  g_notifier = new Notifier(*g_users, *g_sessions, *g_room_registry, *reply_channel);
  g_modlog = new ModLog();
  g_motd = new MotdStore(*g_fs);

  for (size_t i = 0; i < g_room_registry->count(); i++) {
    g_posts->repairRoom(g_room_registry->at(i).id);
  }
  g_mail->repair();
}

size_t handleClientMessage(const uint8_t pub_key[32], const char* input, char* out, size_t out_cap) {
  if (g_users == nullptr) {
    if (out_cap > 0) out[0] = 0;
    return 0;
  }

  uint32_t now_ts = g_clock->nowUnique();

  // Ogni messaggio in arrivo e' anche l'occasione per consegnare eventuali
  // notifiche accorpate gia' pronte (nessun timer/loop dedicato: si
  // aggancia al traffico che la BBS gia' riceve).
  g_notifier->tick(now_ts);

  CommandContext ctx{*g_users,    *g_welcome, *g_posts,         *g_mail, *g_sessions,
                     *g_notifier, *g_rooms,   *g_room_registry, *g_modlog, *g_motd, pub_key, now_ts};
  return processCommand(ctx, input, out, out_cap);
}

namespace {

void printHex(const uint8_t* buf, size_t len) {
  for (size_t i = 0; i < len; i++) Serial.printf("%02X", buf[i]);
}

bool hexNibble(char c, uint8_t& out) {
  if (c >= '0' && c <= '9') { out = (uint8_t)(c - '0'); return true; }
  if (c >= 'a' && c <= 'f') { out = (uint8_t)(10 + (c - 'a')); return true; }
  if (c >= 'A' && c <= 'F') { out = (uint8_t)(10 + (c - 'A')); return true; }
  return false;
}

bool hexDecode(const char* hex, uint8_t* out, size_t out_len) {
  for (size_t i = 0; i < out_len; i++) {
    uint8_t hi, lo;
    if (!hexNibble(hex[i * 2], hi) || !hexNibble(hex[i * 2 + 1], lo)) return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

// Divide 'line' (modificata sul posto: i tab diventano terminatori) in campi
// separati da tab. Ritorna il numero di campi trovati, al piu' max_fields
// (un campo oltre il limite resta attaccato all'ultimo, utile per il testo
// di un post che in teoria potrebbe contenere tab).
size_t splitTabFields(char* line, char** fields, size_t max_fields) {
  size_t n = 0;
  char* p = line;
  fields[n++] = p;
  while (*p != 0 && n < max_fields) {
    if (*p == '\t') {
      *p = 0;
      fields[n++] = p + 1;
    }
    p++;
  }
  return n;
}

enum class ImportSection { NONE, ROOMS, USERS, POSTS };

bool g_import_active = false;
ImportSection g_import_section = ImportSection::NONE;
uint8_t g_import_post_room_id = 0;
uint8_t g_import_rooms_seen = 0;  // bitmask: bit room_id = presente nella sezione [ROOMS] importata
bool g_import_had_error = false;

// Rimuove dal registro (post/iscrizioni/stato apertura inclusi, stesso
// percorso di ROOM DEL) qualunque stanza che il nodo vuoto ha gia' di
// default (Generale/Annunci/Tecnico) ma che la sezione [ROOMS] appena letta
// non elenca: segno che nell'originale era stata cancellata prima
// dell'export, e il ripristino deve rispettarlo.
void pruneRoomsNotSeenDuringImport() {
  uint8_t to_remove[BBS_MAX_ROOMS];
  size_t n = 0;
  for (size_t i = 0; i < g_room_registry->count(); i++) {
    uint8_t id = g_room_registry->at(i).id;
    if ((g_import_rooms_seen & (1u << id)) == 0) to_remove[n++] = id;
  }
  for (size_t i = 0; i < n; i++) {
    g_room_registry->removeRoom(to_remove[i]);
    g_posts->purgeRoom(to_remove[i]);
    g_rooms->setClosed(to_remove[i], false);
  }
}

} // namespace

void exportToSerial() {
  if (g_users == nullptr) return;

  Serial.println("=== BBS EXPORT v1 ===");

  Serial.println("[ROOMS]");
  for (size_t i = 0; i < g_room_registry->count(); i++) {
    const RoomInfo& room = g_room_registry->at(i);
    Serial.printf("%u\t%s\t%s\n", (unsigned)room.id, room.name, g_rooms->isClosed(room.id) ? "CLOSED" : "OPEN");
  }

  Serial.println("[USERS]");
  UserId n_users = g_users->count();
  for (UserId i = 0; i < n_users; i++) {
    char nick[BBS_NICK_LEN];
    if (!g_users->getNickname(i, nick, sizeof(nick))) continue;
    uint8_t pub_key[BBS_PUBKEY_LEN];
    if (!g_users->getPubkey(i, pub_key)) continue;
    UserRole role = g_users->getRole(i);
    const char* role_str = (role == ROLE_ADMIN) ? "admin" : (role == ROLE_MODERATOR) ? "mod" : "user";
    Serial.printf("%lu\t", (unsigned long)i);
    printHex(pub_key, BBS_PUBKEY_LEN);
    Serial.printf("\t%s\t%s\t%s\t%s\n", nick, role_str, g_users->isBanned(i) ? "BANNED" : "-",
                  g_users->isMuted(i) ? "MUTED" : "-");
  }

  // Mail privata e iscrizioni/last_read deliberatamente esclusi: vedi
  // commento su exportToSerial() in bbs_port_adapter.h.
  for (size_t i = 0; i < g_room_registry->count(); i++) {
    const RoomInfo& room = g_room_registry->at(i);
    Serial.printf("[POSTS room=%u]\n", (unsigned)room.id);

    PostRecord pinned;
    bool has_pinned = g_posts->findPinned(room.id, pinned);

    uint32_t after_ts = 0;
    PostRecord post;
    while (g_posts->findNextUnread(room.id, after_ts, post)) {
      char author[BBS_NICK_LEN];
      if (!g_users->getNickname(post.user_id, author, sizeof(author))) {
        strncpy(author, "???", sizeof(author));
        author[sizeof(author) - 1] = 0;
      }
      // Un post e' identificato per il confronto da timestamp+autore: non ha
      // un id proprio (vedi PostRecord), ma nowUnique() li rende univoci
      // quanto basta — stessa assunzione gia' usata altrove (es. il cursore
      // "ultimo letto").
      bool is_pinned = has_pinned && pinned.timestamp == post.timestamp && pinned.user_id == post.user_id;
      Serial.printf("%lu\t%s\t%s\t%s\n", (unsigned long)post.timestamp, author, is_pinned ? "PIN" : "-", post.text);
      after_ts = post.timestamp;
    }
  }

  Serial.println("=== FINE EXPORT ===");
}

bool importStart() {
  if (g_users == nullptr) return false;
  if (g_users->count() > 0) return false;  // solo su nodo vuoto, vedi bbs_port_adapter.h

  g_import_active = true;
  g_import_section = ImportSection::NONE;
  g_import_rooms_seen = 0;
  g_import_had_error = false;
  return true;
}

bool importInProgress() { return g_import_active; }

void importFeedLine(const char* line_in) {
  if (!g_import_active) return;

  char line[200];
  strncpy(line, line_in, sizeof(line) - 1);
  line[sizeof(line) - 1] = 0;

  if (line[0] == 0) return;  // riga vuota, ignorata
  if (strcmp(line, "=== BBS EXPORT v1 ===") == 0) {
    g_import_section = ImportSection::NONE;
    return;
  }
  if (strcmp(line, "=== FINE EXPORT ===") == 0) {
    g_import_active = false;
    Serial.println(g_import_had_error ? "Import completato CON ERRORI (vedi sopra)." : "Import completato.");
    return;
  }
  if (strcmp(line, "[ROOMS]") == 0) {
    g_import_section = ImportSection::ROOMS;
    return;
  }
  if (strcmp(line, "[USERS]") == 0) {
    pruneRoomsNotSeenDuringImport();  // sezione [ROOMS] conclusa
    g_import_section = ImportSection::USERS;
    return;
  }
  if (strncmp(line, "[POSTS room=", 12) == 0) {
    g_import_section = ImportSection::POSTS;
    g_import_post_room_id = (uint8_t)atoi(line + 12);
    return;
  }

  char* fields[6];

  switch (g_import_section) {
    case ImportSection::ROOMS: {
      if (splitTabFields(line, fields, 3) != 3) {
        Serial.printf("Import: riga stanza malformata: %s\n", line);
        g_import_had_error = true;
        return;
      }
      uint8_t id = (uint8_t)atoi(fields[0]);
      const char* name = fields[1];
      bool closed = (strcmp(fields[2], "CLOSED") == 0);
      if (id >= BBS_MAX_ROOMS) {
        Serial.printf("Import: id stanza fuori range: %s\n", line);
        g_import_had_error = true;
        return;
      }
      g_import_rooms_seen |= (uint8_t)(1u << id);

      // Un nodo vuoto parte gia' con Generale/Annunci/Tecnico (vedi
      // RoomRegistry): se l'id esiste gia' con lo stesso ruolo di default
      // non c'e' nulla da aggiungere, altrimenti ricreala — l'id assegnato
      // da addRoom (il piu' basso libero) deve combaciare, visto che siamo
      // partiti da un registro vuoto nello stesso ordine dell'export.
      if (g_room_registry->findRoom(id) == nullptr) {
        uint8_t new_id;
        AddRoomResult r = g_room_registry->addRoom(name, new_id);
        if (r != AddRoomResult::OK || new_id != id) {
          Serial.printf("Import: impossibile ricreare la stanza %u:%s (risultato %d, id ottenuto %u)\n",
                        (unsigned)id, name, (int)r, (unsigned)new_id);
          g_import_had_error = true;
          return;
        }
      }
      g_rooms->setClosed(id, closed);
      break;
    }
    case ImportSection::USERS: {
      if (splitTabFields(line, fields, 6) != 6) {
        Serial.printf("Import: riga utente malformata: %s\n", line);
        g_import_had_error = true;
        return;
      }
      const char* pubkey_hex = fields[1];
      const char* nickname = fields[2];
      const char* role_str = fields[3];
      bool banned = (strcmp(fields[4], "BANNED") == 0);
      bool muted = (strcmp(fields[5], "MUTED") == 0);

      uint8_t pub_key[BBS_PUBKEY_LEN];
      if (strlen(pubkey_hex) != BBS_PUBKEY_LEN * 2 || !hexDecode(pubkey_hex, pub_key, BBS_PUBKEY_LEN)) {
        Serial.printf("Import: pubkey non valida per l'utente '%s'\n", nickname);
        g_import_had_error = true;
        return;
      }

      UserId new_id;
      RegisterResult r = g_users->registerUser(pub_key, nickname, g_clock->now(), new_id);
      if (r != RegisterResult::OK) {
        Serial.printf("Import: impossibile registrare '%s' (risultato %d)\n", nickname, (int)r);
        g_import_had_error = true;
        return;
      }
      UserRole role = (strcmp(role_str, "admin") == 0)   ? ROLE_ADMIN
                      : (strcmp(role_str, "mod") == 0)    ? ROLE_MODERATOR
                                                           : ROLE_USER;
      g_users->setRole(new_id, role);
      if (banned) g_users->setBanned(new_id, true);
      if (muted) g_users->setMuted(new_id, true);
      break;
    }
    case ImportSection::POSTS: {
      if (splitTabFields(line, fields, 4) != 4) {
        Serial.printf("Import: riga post malformata in stanza %u: %s\n", (unsigned)g_import_post_room_id, line);
        g_import_had_error = true;
        return;
      }
      uint32_t ts = (uint32_t)strtoul(fields[0], nullptr, 10);
      const char* author_nick = fields[1];
      bool pinned = (strcmp(fields[2], "PIN") == 0);
      const char* text = fields[3];

      UserId author = g_users->findByNickname(author_nick);
      if (author == kInvalidUserId) {
        Serial.printf("Import: autore '%s' non trovato, post in stanza %u saltato\n", author_nick,
                      (unsigned)g_import_post_room_id);
        g_import_had_error = true;
        return;
      }
      if (!g_posts->appendPost(g_import_post_room_id, author, ts, text)) {
        Serial.printf("Import: impossibile scrivere un post in stanza %u\n", (unsigned)g_import_post_room_id);
        g_import_had_error = true;
        return;
      }
      if (pinned) g_posts->pinLastPost(g_import_post_room_id);
      break;
    }
    case ImportSection::NONE:
    default:
      break;  // righe fuori sezione: ignorate
  }
}

} // namespace port
} // namespace bbs
