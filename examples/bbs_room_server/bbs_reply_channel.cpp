// Implementazione di bbs::port::MyMeshReplyChannel (dichiarata in
// lib/bbs_port/bbs_port_reply_channel.h). Vive qui, come sorgente
// dell'esempio, e non in lib/bbs_port/, perche' deve includere per intero
// MyMesh.h (che qui compila gia' correttamente, essendo il file che la
// definisce): dentro lib/bbs_port/ lo stesso include non trova gli header
// del framework (es. LittleFS.h) che raggiunge tramite MyMesh.h, perche'
// la libreria non eredita i percorsi di include che il sorgente di
// progetto ha per il resto del firmware.

#include "MyMesh.h"

#include "../../lib/bbs_port/bbs_port_reply_channel.h"

namespace bbs {
namespace port {

bool MyMeshReplyChannel::sendReply(const uint8_t pub_key[BBS_PUBKEY_LEN], const uint8_t* payload, size_t len) {
  ClientInfo* client = _mesh->getBbsClient(pub_key);
  if (!client) return false;
  return _mesh->sendBbsReply(client, payload, len);
}

} // namespace port
} // namespace bbs
