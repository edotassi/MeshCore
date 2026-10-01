#pragma once

#include "bbs_config.h"
#include "bbs_port.h"

namespace bbs {

// Stato aperta/chiusa di ogni stanza (Fase 3, moderazione), persistito in
// un file minuscolo (BBS_MAX_ROOMS byte, uno per stanza). Assente = tutte
// aperte (default). Una stanza chiusa rifiuta nuovi post (E) ma resta
// leggibile (N).
class RoomState {
public:
  explicit RoomState(IFileSystem& fs, const char* path = "/bbs/rooms.dat");

  bool isClosed(uint8_t room_id) const;
  bool setClosed(uint8_t room_id, bool closed);

private:
  IFileSystem& _fs;
  const char* _path;
};

} // namespace bbs
