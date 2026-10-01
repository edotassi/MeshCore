#pragma once

#include <cstring>
#include <string>
#include <vector>

#include "bbs_port.h"

// Canale di risposta finto per i test nativi: registra ogni invio invece di
// mandarlo davvero da qualche parte.
namespace bbs_test {

class FakeReplyChannel : public bbs::IReplyChannel {
public:
  struct Sent {
    uint8_t pub_key[bbs::BBS_PUBKEY_LEN];
    std::string text;
  };

  bool fail_next = false;  // per simulare un mancato recapito nel prossimo invio

  bool sendReply(const uint8_t pub_key[bbs::BBS_PUBKEY_LEN], const uint8_t* payload, size_t len) override {
    if (fail_next) {
      fail_next = false;
      return false;
    }
    Sent s;
    memcpy(s.pub_key, pub_key, bbs::BBS_PUBKEY_LEN);
    s.text.assign((const char*)payload, len);
    sent.push_back(s);
    return true;
  }

  std::vector<Sent> sent;
};

} // namespace bbs_test
