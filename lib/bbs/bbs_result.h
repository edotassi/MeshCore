#pragma once

namespace bbs {

enum class RegisterResult {
  OK,
  ALREADY_EXISTS,
  INVALID_NICKNAME,
  STORE_FULL,
  IO_ERROR,
};

} // namespace bbs
