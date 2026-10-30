#pragma once

#include <cstddef>
#include <cstdint>

#include "bbs_port.h"

// Persistenza dei post del room server (anello in RAM di MyMesh). Parte pura:
// dipende solo da bbs::IFileSystem e dal CRC16 della BBS, nessun tipo Arduino
// o MeshCore. Tutti i file vivono sotto /room/, mai in /bbs/.
//
// Formato: intestazione (magic "RPST", versione, conteggio u32) e poi i
// record, dal piu' vecchio al piu' recente. Ogni record:
//   pub_key[32], timestamp u32, lunghezza testo u8, testo, crc16 u16
// con il CRC calcolato su tutti i byte che lo precedono nel record.

namespace room {

constexpr size_t kPubKeyLen = 32;
constexpr size_t kMaxPostTextLen = 151;  // come MAX_POST_TEXT_LEN del room server

constexpr const char* kPostsPath = "/room/posts.dat";
constexpr const char* kPostsTmpPath = "/room/posts.tmp";

struct PostRecord {
  uint8_t pub_key[kPubKeyLen];
  uint32_t timestamp;
  char text[kMaxPostTextLen + 1];  // terminato da zero
};

// Sorgente dei record da salvare, per non dover copiare l'intero anello.
class IPostSource {
public:
  virtual ~IPostSource() = default;
  // Riempie 'out' con l'i-esimo record (0 = il piu' vecchio). false = errore.
  virtual bool get(size_t index, PostRecord& out) const = 0;
};

class PostFile {
public:
  explicit PostFile(bbs::IFileSystem& fs) : _fs(fs) {}

  // Scrive /room/posts.tmp, poi remove + rename su /room/posts.dat.
  bool save(const PostRecord* recs, size_t n);
  bool save(const IPostSource& src, size_t n);

  // Carica fino a 'cap' record, dal piu' vecchio al piu' recente. Salta i
  // record con CRC errato, si ferma senza errori su un file troncato; file
  // assente, illeggibile o di versione ignota = 0 record. Se il file ne ha
  // di piu', tiene i 'cap' piu' recenti.
  size_t load(PostRecord* out, size_t cap);

private:
  bbs::IFileSystem& _fs;
};

} // namespace room
