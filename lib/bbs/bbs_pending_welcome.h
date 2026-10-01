#pragma once

#include "bbs_config.h"
#include "bbs_port.h"

namespace bbs {

// File append-only di pubkey gia' accolte con il messaggio di benvenuto
// completo, per garantirne l'invio una sola volta a chi non si registra.
class PendingWelcomeTable {
public:
  explicit PendingWelcomeTable(IFileSystem& fs, const char* path = "/bbs/welcomed.dat");

  bool isWelcomed(const uint8_t pub_key[BBS_PUBKEY_LEN]) const;
  bool markWelcomed(const uint8_t pub_key[BBS_PUBKEY_LEN]);

private:
  IFileSystem& _fs;
  const char* _path;
};

} // namespace bbs
