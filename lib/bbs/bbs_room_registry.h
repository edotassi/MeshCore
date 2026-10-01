#pragma once

#include <cstddef>
#include <cstdint>

#include "bbs_config.h"

// Elenco statico delle stanze attive. In una fase successiva (Fase 3,
// "Configurazione") diventera' modificabile a runtime; per ora vive qui,
// non nel codice del parser, cosi' aggiungerne una e' un cambio isolato.

namespace bbs {

struct RoomInfo {
  uint8_t id;
  const char* name;
};

constexpr RoomInfo kRooms[] = {
    {0, "Generale"},
    {1, "Annunci"},
    {2, "Tecnico"},
};
constexpr size_t kNumRooms = sizeof(kRooms) / sizeof(kRooms[0]);

static_assert(kNumRooms <= BBS_MAX_ROOMS, "troppe stanze rispetto a BBS_MAX_ROOMS");

// Iscrizione di default alla registrazione: solo le stanze principali
// (Generale, Annunci — bit 0 e 1); il resto (es. Tecnico) e' opt-in con S.
constexpr uint8_t kDefaultSubscribedMask = (1u << 0) | (1u << 1);

inline const RoomInfo* findRoom(uint8_t id) {
  for (size_t i = 0; i < kNumRooms; i++) {
    if (kRooms[i].id == id) return &kRooms[i];
  }
  return nullptr;
}

} // namespace bbs
