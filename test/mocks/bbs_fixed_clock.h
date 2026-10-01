#pragma once

#include <cstdint>

#include "bbs_port.h"

namespace bbs_test {

class FixedClock : public bbs::IClock {
public:
  explicit FixedClock(uint32_t t = 1000) : _t(t) {}
  uint32_t now() const override { return _t; }
  uint32_t nowUnique() const override { return _t; }
  void set(uint32_t t) { _t = t; }

private:
  uint32_t _t;
};

} // namespace bbs_test
