#include "bbs_port_adapter.h"

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
  g_notifier = new Notifier(*g_users, *g_sessions, *reply_channel);
  g_rooms = new RoomState(*g_fs);
  g_modlog = new ModLog();
  g_motd = new MotdStore(*g_fs);

  for (size_t i = 0; i < kNumRooms; i++) {
    g_posts->repairRoom(kRooms[i].id);
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

  CommandContext ctx{*g_users, *g_welcome, *g_posts,    *g_mail,  *g_sessions,
                     *g_notifier, *g_rooms,  *g_modlog, *g_motd,  pub_key, now_ts};
  return processCommand(ctx, input, out, out_cap);
}

} // namespace port
} // namespace bbs
