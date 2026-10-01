#pragma once

#include "../bbs/bbs_port.h"

// Dichiara solo un puntatore opaco a MyMesh (definita in
// examples/bbs_room_server/MyMesh.h, che a sua volta include
// bbs_port_adapter.h): l'implementazione, in bbs_port_reply_channel.cpp,
// include MyMesh.h per intero. Questo evita l'inclusione circolare che si
// avrebbe se questo header richiedesse la definizione completa di MyMesh.
class MyMesh;

namespace bbs {
namespace port {

class MyMeshReplyChannel : public IReplyChannel {
public:
  explicit MyMeshReplyChannel(MyMesh* mesh) : _mesh(mesh) {}

  bool sendReply(const uint8_t pub_key[BBS_PUBKEY_LEN], const uint8_t* payload, size_t len) override;

private:
  MyMesh* _mesh;
};

} // namespace port
} // namespace bbs
