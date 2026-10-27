#include "bbs_port_adapter.h"

#include <Arduino.h>
#include <Utils.h>

#include <cstring>

#include "../bbs/bbs_bot.h"
#include "../bbs/bbs_command_parser.h"
#include "../bbs/bbs_digest.h"
#include "../bbs/bbs_mail_store.h"
#include "../bbs/bbs_mod_log.h"
#include "../bbs/bbs_motd.h"
#include "../bbs/bbs_news.h"
#include "../bbs/bbs_news_broadcast.h"
#include "../bbs/bbs_notifier.h"
#include "../bbs/bbs_pending_welcome.h"
#include "../bbs/bbs_post_store.h"
#include "../bbs/bbs_room_registry.h"
#include "../bbs/bbs_room_state.h"
#include "../bbs/bbs_session_table.h"
#include "../bbs/bbs_strings_it.h"
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
DigestStore* g_digest = nullptr;
BotStore* g_bot = nullptr;
BotCooldown* g_bot_cooldown = nullptr;
NewsStore* g_news = nullptr;
NewsSeen* g_news_seen = nullptr;
UserId g_system_user = kInvalidUserId;  // autore dei post delle news, vedi setNodeIdentity

// Canali del bot con la chiave gia' risolta, ricalcolata quando cambia
// BotStore::generation().
mesh::GroupChannel g_bot_channels[kBotMaxChannels];
size_t g_bot_channel_count = 0;
uint32_t g_bot_resolved_gen = 0;
bool g_bot_resolved = false;

// Canale di trasmissione delle news con la chiave risolta, ricalcolato quando
// cambia NewsStore::generation(). Se cambia il canale (o viene spento) la coda
// in attesa e' scartata.
NewsBroadcastQueue g_news_queue;
mesh::GroupChannel g_news_channel;
BotChannel g_news_channel_src;  // impostazione da cui deriva g_news_channel
bool g_news_channel_on = false;
uint32_t g_news_resolved_gen = 0;
bool g_news_resolved = false;

// Copia una stringa C troncandola, sempre terminata.
void copyCString(char* dst, const char* src, size_t cap) {
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = 0;
}

// Chiave e hash di un canale del bot o delle news (hashtag: primi 16 byte
// dello SHA-256 del nome, minuscolo, '#' incluso, come i client MeshCore).
void resolveChannel(const BotChannel& c, mesh::GroupChannel& ch) {
  memset(ch.secret, 0, sizeof(ch.secret));
  size_t key_len = c.psk_len;
  if (c.isHashtag()) {
    uint8_t digest[32];
    mesh::Utils::sha256(digest, sizeof(digest), (const uint8_t*)c.name, (int)strlen(c.name));
    memcpy(ch.secret, digest, 16);
    key_len = 16;
  } else {
    memcpy(ch.secret, c.psk, key_len);
  }
  // Stesso hash che BaseChatMesh::addChannel calcola dalla chiave.
  mesh::Utils::sha256(ch.hash, sizeof(ch.hash), ch.secret, (int)key_len);
}

void resolveBotChannels() {
  if (g_bot == nullptr) return;
  if (g_bot_resolved && g_bot_resolved_gen == g_bot->generation()) return;

  const BotSettings& s = g_bot->get();
  g_bot_channel_count = s.count;
  for (size_t i = 0; i < s.count; i++) {
    const BotChannel& c = s.channels[i];
    resolveChannel(c, g_bot_channels[i]);
  }
  g_bot_resolved_gen = g_bot->generation();
  g_bot_resolved = true;
}

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
  g_notifier = new Notifier(*g_users, *g_sessions, *g_room_registry, *g_posts, *reply_channel);
  g_modlog = new ModLog();
  g_motd = new MotdStore(*g_fs);
  g_digest = new DigestStore(*g_fs);
  g_bot = new BotStore(*g_fs);
  g_bot_cooldown = new BotCooldown();
  g_news = new NewsStore(*g_fs);
  g_news_seen = new NewsSeen(*g_fs);

  for (size_t i = 0; i < g_room_registry->count(); i++) {
    g_posts->repairRoom(g_room_registry->at(i).id);
  }
  g_mail->repair();
}

void setNodeIdentity(const uint8_t pub_key[32]) {
  if (g_users == nullptr) return;
  g_system_user = g_users->ensureSystemUser(pub_key, "News", g_clock->now());
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

  CommandContext ctx{*g_users, *g_welcome, *g_posts,         *g_mail,   *g_sessions,
                     *g_rooms, *g_room_registry, *g_modlog, *g_motd, *g_digest, *g_bot, *g_news, pub_key, now_ts};
  return processCommand(ctx, input, out, out_cap);
}

size_t handleSerialAdminCommand(const char* input, char* out, size_t out_cap) {
  if (out_cap > 0) out[0] = 0;
  if (g_users == nullptr || out_cap == 0) return 0;

  // Si agisce come il primo amministratore registrato (non di sistema): la
  // console seriale e' gia' un canale amministrativo (export/import/erase).
  uint8_t admin_key[BBS_PUBKEY_LEN];
  bool found = false;
  for (UserId id = 0; id < g_users->count() && !found; id++) {
    if (g_users->isSystemUser(id) || g_users->getRole(id) != ROLE_ADMIN) continue;
    found = g_users->getPubkey(id, admin_key);
  }
  if (!found) {
    copyCString(out, "Nessun amministratore registrato.", out_cap);
    return strlen(out);
  }

  uint32_t now_ts = g_clock->nowUnique();
  CommandContext ctx{*g_users, *g_welcome, *g_posts,         *g_mail,   *g_sessions,
                     *g_rooms, *g_room_registry, *g_modlog, *g_motd, *g_digest, *g_bot, *g_news, admin_key, now_ts};
  return processCommand(ctx, input, out, out_cap);
}

bool pollDigest(char* text, size_t text_cap, uint8_t psk[32], size_t& psk_len) {
  if (g_digest == nullptr || text_cap == 0) return false;

  // Richiesta manuale (DIGEST NOW): parte comunque, anche se disattivato o
  // senza post, e non tocca last_sent_day.
  bool send = false;
  uint32_t now = g_clock->now();
  if (g_digest->takeNowRequest()) {
    if (formatDigest(*g_posts, *g_room_registry, now, text, text_cap) == 0) {
      strncpy(text, strings::kDigestNoPosts, text_cap - 1);
      text[text_cap - 1] = 0;
    }
    send = true;
  } else {
    DigestDecision d = decide(g_digest->get(), now);
    if (d == DigestDecision::WAIT) return false;

    // Segna il giorno come fatto PRIMA di inviare: se la scrittura fallisce
    // non si invia, altrimenti si rimanderebbe ogni 30 secondi.
    DigestSettings s = g_digest->get();
    s.last_sent_day = localDayNumber(localTime(now, s));
    if (!g_digest->save(s)) return false;

    if (d == DigestDecision::SEND &&
        formatDigest(*g_posts, *g_room_registry, now, text, text_cap) > 0) {
      send = true;
    }
  }
  if (!send) return false;

  const DigestSettings& s = g_digest->get();
  memset(psk, 0, 32);
  memcpy(psk, s.psk, s.psk_len);
  psk_len = s.psk_len;
  return true;
}

int botSearchChannels(const uint8_t* hash, mesh::GroupChannel out[], int max) {
  if (g_bot == nullptr || !g_bot->get().enabled) return 0;
  resolveBotChannels();
  int n = 0;
  for (size_t i = 0; i < g_bot_channel_count && n < max; i++) {
    if (memcmp(g_bot_channels[i].hash, hash, sizeof(g_bot_channels[i].hash)) == 0) out[n++] = g_bot_channels[i];
  }
  return n;
}

int botChannelIndex(const mesh::GroupChannel& ch) {
  if (g_bot == nullptr) return -1;
  resolveBotChannels();
  for (size_t i = 0; i < g_bot_channel_count; i++) {
    if (memcmp(g_bot_channels[i].secret, ch.secret, sizeof(ch.secret)) == 0) return (int)i;
  }
  return -1;
}

size_t botHandleGroupText(int channel_idx, const char* text, uint8_t hop_count, const uint8_t* path,
                          uint8_t hash_size, unsigned long now_ms, char* out, size_t out_cap) {
  if (g_bot == nullptr || !g_bot->get().enabled || out_cap == 0) return 0;
  if (channel_idx < 0 || (size_t)channel_idx >= g_bot->get().count) return 0;

  char sender[40];
  BotCommand cmd = parseBotCommand(text, sender, sizeof(sender));
  if (cmd == BotCommand::NONE) return 0;
  // Il cooldown (condiviso tra "test" e "!news") si consuma solo se la
  // risposta parte davvero.
  if (!g_bot_cooldown->allow((size_t)channel_idx, (uint32_t)now_ms)) return 0;

  if (cmd == BotCommand::NEWS) {
    PostRecord latest;
    const char* news_text = nullptr;
    uint8_t room_id = g_news->get().room_id;
    if (room_id != kNoRoom && g_room_registry->findRoom(room_id) != nullptr &&
        g_posts->findLatest(room_id, latest)) {
      news_text = latest.text;
    }
    return formatNewsReply(sender, news_text, out, out_cap);
  }
  return formatHopReply(sender, hop_count, path, hash_size, out, out_cap);
}

bool newsGetConfig(NewsClientConfig& out, uint32_t& generation) {
  if (g_news == nullptr) return false;
  const NewsSettings& s = g_news->get();
  // Con l'orologio non ancora impostato non ci si collega: i post avrebbero
  // timestamp sbagliati, e restando disconnessi il broker tiene in coda le
  // news (sessione persistente) finche' l'orologio non e' valido.
  out.enabled = s.enabled && g_clock->now() >= kDigestMinValidEpoch;
  copyCString(out.server, s.server, sizeof(out.server));
  out.port = s.port;
  copyCString(out.username, s.username, sizeof(out.username));
  copyCString(out.password, s.password, sizeof(out.password));
  copyCString(out.topic, s.topic, sizeof(out.topic));
  generation = g_news->generation();
  return true;
}

void newsSetConnected(bool connected) {
  if (g_news != nullptr) g_news->setConnected(connected);
}

int newsIngest(const uint8_t* payload, size_t len, const char** name) {
  if (g_news == nullptr) return -1;
  uint32_t now = g_clock->now();
  bool clock_valid = now >= kDigestMinValidEpoch;
  // nowUnique(): due news nello stesso secondo non devono avere lo stesso
  // timestamp, altrimenti il cursore "ultimo letto" ne salterebbe una.
  char posted[BBS_MAX_TEXT_LEN + 1];
  NewsIngestResult r = ingestNews(*g_news, *g_news_seen, *g_posts, *g_room_registry, *g_users, g_system_user,
                                  (const char*)payload, len, g_clock->nowUnique(), clock_valid, posted);
  if (r == NewsIngestResult::OK && g_news->get().channel_enabled) g_news_queue.push(posted);
  if (name != nullptr) *name = newsResultName(r);
  return (int)r;
}

bool newsPollBroadcast(char* text, size_t text_cap, mesh::GroupChannel& ch, unsigned long now_ms) {
  if (g_news == nullptr || text_cap == 0) return false;
  const NewsSettings& s = g_news->get();

  if (!s.channel_enabled) {
    g_news_queue.clear();
    g_news_channel_on = false;
    return false;
  }
  if (!g_news_resolved || g_news_resolved_gen != g_news->generation()) {
    // Cambio di canale (o riattivazione): le news in attesa erano per un altro.
    bool same = g_news_channel_on && strcmp(g_news_channel_src.name, s.channel.name) == 0 &&
                g_news_channel_src.psk_len == s.channel.psk_len &&
                memcmp(g_news_channel_src.psk, s.channel.psk, sizeof(s.channel.psk)) == 0;
    if (!same) g_news_queue.clear();
    g_news_channel_src = s.channel;
    g_news_channel_on = true;
    resolveChannel(s.channel, g_news_channel);
    g_news_resolved_gen = g_news->generation();
    g_news_resolved = true;
  }

  if (!g_news_queue.poll((uint32_t)now_ms, text, text_cap)) return false;
  ch = g_news_channel;
  return true;
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
    const char* role_str = g_users->isSystemUser(i)   ? "system"
                           : (role == ROLE_ADMIN)      ? "admin"
                           : (role == ROLE_MODERATOR)  ? "mod"
                                                       : "user";
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
  // Solo su nodo vuoto, vedi bbs_port_adapter.h: l'utente di sistema delle
  // news, creato all'avvio, non conta.
  if (g_users->countNonSystem() > 0) return false;

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

      // Utente di sistema (autore delle news): esiste gia' con la chiave del
      // nodo corrente (setNodeIdentity), quindi non si registra come utente
      // normale e non diventa mai admin. Se la chiave e' quella di un nodo
      // precedente viene ripristinato come un altro utente di sistema, cosi'
      // i suoi post restano attribuibili; altrimenti non c'e' nulla da fare.
      if (strcmp(role_str, "system") == 0) {
        if (g_users->ensureSystemUser(pub_key, nickname, g_clock->now()) == kInvalidUserId) {
          Serial.printf("Import: impossibile ripristinare l'utente di sistema '%s'\n", nickname);
          g_import_had_error = true;
        }
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
