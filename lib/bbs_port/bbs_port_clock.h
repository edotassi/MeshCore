#pragma once

#include <MeshCore.h>

#include "../bbs/bbs_port.h"

// Implementazione concreta di IClock sopra mesh::RTCClock. Unico punto
// della BBS che include header MeshCore per l'orologio.

namespace bbs {
namespace port {

class MeshCoreClock : public IClock {
public:
  explicit MeshCoreClock(mesh::RTCClock* clock) : _clock(clock) {}

  uint32_t now() const override { return _clock->getCurrentTime(); }
  uint32_t nowUnique() const override { return _clock->getCurrentTimeUnique(); }

private:
  mesh::RTCClock* _clock;
};

} // namespace port
} // namespace bbs
