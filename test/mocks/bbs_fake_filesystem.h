#pragma once

#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "bbs_port.h"

// Filesystem in-memoria per i test nativi della BBS: nessun accesso al
// disco reale, cosi' la logica pura in lib/bbs/ si verifica senza hardware.
namespace bbs_test {

class FakeFile : public bbs::IFile {
public:
  FakeFile() : _data(nullptr), _pos(0), _writable(false), _valid(false) {}
  FakeFile(std::vector<uint8_t>* data, size_t pos, bool writable, bool valid)
      : _data(data), _pos(pos), _writable(writable), _valid(valid) {}

  size_t read(uint8_t* buf, size_t len) override {
    if (!_valid || !_data) return 0;
    size_t avail = (_pos < _data->size()) ? (_data->size() - _pos) : 0;
    size_t n = (len < avail) ? len : avail;
    if (n > 0) memcpy(buf, _data->data() + _pos, n);
    _pos += n;
    return n;
  }

  size_t write(const uint8_t* buf, size_t len) override {
    if (!_valid || !_data || !_writable) return 0;
    if (_pos + len > _data->size()) _data->resize(_pos + len);
    memcpy(_data->data() + _pos, buf, len);
    _pos += len;
    return len;
  }

  bool seek(uint32_t offset) override {
    if (!_valid) return false;
    _pos = offset;
    return true;
  }

  uint32_t size() const override { return (_valid && _data) ? (uint32_t)_data->size() : 0; }
  void close() override { _valid = false; }
  bool valid() const override { return _valid; }

private:
  std::vector<uint8_t>* _data;
  size_t _pos;
  bool _writable;
  bool _valid;
};

class FakeFileSystem : public bbs::IFileSystem {
public:
  bbs::IFile* open(const char* path, char mode) override {
    std::string p(path);
    if (mode == 'r') {
      auto it = _files.find(p);
      if (it == _files.end()) {
        _handles.emplace_back();
      } else {
        _handles.emplace_back(&it->second, 0, false, true);
      }
    } else if (mode == 'a') {
      auto& vec = _files[p];
      _handles.emplace_back(&vec, vec.size(), true, true);
    } else if (mode == 'w') {
      auto& vec = _files[p];
      vec.clear();
      _handles.emplace_back(&vec, 0, true, true);
    } else {
      _handles.emplace_back();
    }
    return &_handles.back();
  }

  bool remove(const char* path) override { return _files.erase(std::string(path)) > 0; }

  bool rename(const char* from, const char* to) override {
    auto it = _files.find(std::string(from));
    if (it == _files.end()) return false;
    _files[std::string(to)] = std::move(it->second);
    _files.erase(it);
    return true;
  }

private:
  std::map<std::string, std::vector<uint8_t>> _files;
  std::deque<FakeFile> _handles;  // deque: gli indirizzi restano stabili quando si aggiunge un handle
};

} // namespace bbs_test
