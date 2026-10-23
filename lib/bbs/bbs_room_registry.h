#pragma once

#include <cstddef>
#include <cstdint>

#include "bbs_config.h"
#include "bbs_port.h"

namespace bbs {

struct RoomInfo {
  uint8_t id;
  char name[BBS_ROOM_NAME_LEN];
};

enum class AddRoomResult { OK, FULL, INVALID_NAME, DUPLICATE_NAME, IO_ERROR };
enum class RemoveRoomResult { OK, NOT_FOUND, IO_ERROR };

// Stanza in cui si trova di default un utente appena registrato (Generale).
// Resta una costante di compilazione anche ora che l'elenco e' dinamico: e'
// una scelta di prodotto, non una proprieta' della singola stanza.
constexpr uint8_t kDefaultRoomId = 0;

// Elenco delle stanze attive (Fase 5, "Configurazione"): non piu' un array
// costante a compile-time, ma un piccolo registro persistito su file (stesso
// schema "lazy" di bbs_room_state.h), cosi' ADD/DEL via comando admin
// sopravvivono al riavvio. Assente = i tre default storici (Generale,
// Annunci, Tecnico): nessun file scritto finche' non arriva la prima
// modifica. Tenuto in RAM (al piu' BBS_MAX_ROOMS voci, poche decine di byte)
// e riscritto per intero a ogni ADD/DEL, con lo stesso pattern
// "persisti-poi-applica" usato altrove nel codebase: se la scrittura su file
// fallisce, lo stato in RAM non cambia.
class RoomRegistry {
public:
  explicit RoomRegistry(IFileSystem& fs, const char* path = "/bbs/room_registry.dat");

  size_t count() const { return _count; }
  const RoomInfo& at(size_t index) const { return _rooms[index]; }
  const RoomInfo* findRoom(uint8_t id) const;

  // Il nuovo id e' il piu' basso libero in [0, BBS_MAX_ROOMS): il bitmask di
  // iscrizione e last_read[] nel record utente (bbs_types.h) sono indicizzati
  // direttamente da room_id, non dalla posizione in questo elenco.
  AddRoomResult addRoom(const char* name, uint8_t& out_id);

  // Rimuove solo la voce dall'elenco: non toglie post/iscrizioni/stato
  // apertura associati a room_id — sta al chiamante (vedi
  // bbs_command_parser.cpp) pulire anche quelli, visto che l'id verra'
  // riassegnato a una stanza futura.
  RemoveRoomResult removeRoom(uint8_t id);

  static bool isValidName(const char* name);

private:
  IFileSystem& _fs;
  const char* _path;
  RoomInfo _rooms[BBS_MAX_ROOMS];
  size_t _count = 0;

  void load();
  bool persist(const RoomInfo* rooms, size_t count) const;
};

} // namespace bbs
