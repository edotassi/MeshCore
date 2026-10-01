#pragma once

#include <FS.h>

#include "../bbs/bbs_port.h"

// Implementazione concreta di IFileSystem/IFile sopra fs::FS (LittleFS su
// ESP32). Unico punto della BBS che include header Arduino/framework per
// il filesystem.

namespace bbs {
namespace port {

class LittleFsFile : public IFile {
public:
  LittleFsFile() : _valid(false) {}

  void bind(fs::File file) {
    if (_file) _file.close();
    _file = file;
    _valid = (bool)_file;
  }

  size_t read(uint8_t* buf, size_t len) override { return _valid ? _file.read(buf, len) : 0; }
  size_t write(const uint8_t* buf, size_t len) override { return _valid ? _file.write(buf, len) : 0; }
  bool seek(uint32_t offset) override { return _valid && _file.seek(offset); }
  uint32_t size() const override { return _valid ? (uint32_t)_file.size() : 0; }
  void close() override {
    if (_valid) _file.close();
    _valid = false;
  }
  bool valid() const override { return _valid; }

private:
  fs::File _file;
  bool _valid;
};

// Pool statico di slot: nessuna allocazione dopo il setup. Il codice BBS
// non apre mai piu' di 2 file contemporaneamente (es. sorgente+temporaneo
// durante una riparazione o un aggiornamento in-place); 3 slot lasciano un
// margine.
class LittleFsFileSystem : public IFileSystem {
public:
  explicit LittleFsFileSystem(fs::FS& fs) : _fs(fs), _next_slot(0) {}

  IFile* open(const char* path, char mode) override {
    LittleFsFile& slot = _slots[_next_slot];
    _next_slot = (_next_slot + 1) % kNumSlots;

    const char* fs_mode = (mode == 'a') ? "a" : (mode == 'w') ? "w" : "r";
    bool create = (mode == 'a' || mode == 'w');
    slot.bind(_fs.open(path, fs_mode, create));
    return &slot;
  }

  bool remove(const char* path) override { return _fs.remove(path); }
  bool rename(const char* from, const char* to) override { return _fs.rename(from, to); }

private:
  static constexpr int kNumSlots = 3;
  fs::FS& _fs;
  LittleFsFile _slots[kNumSlots];
  int _next_slot;
};

} // namespace port
} // namespace bbs
