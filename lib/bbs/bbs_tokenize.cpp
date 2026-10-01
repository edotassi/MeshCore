#include "bbs_tokenize.h"

namespace bbs {

const char* extractToken(const char* input, char* token_out, size_t token_cap) {
  if (token_cap == 0) return input;

  const char* p = input;
  while (*p == ' ' || *p == '\t') p++;

  size_t n = 0;
  while (*p != 0 && *p != ' ' && *p != '\t') {
    if (n + 1 < token_cap) token_out[n++] = *p;
    p++;
  }
  token_out[n] = 0;

  while (*p == ' ' || *p == '\t') p++;
  return p;
}

const char* extractCommand(const char* input, char* cmd_out, size_t cmd_cap) {
  const char* rest = extractToken(input, cmd_out, cmd_cap);
  for (size_t i = 0; cmd_out[i] != 0; i++) {
    char c = cmd_out[i];
    if (c >= 'a' && c <= 'z') cmd_out[i] = (char)(c - 'a' + 'A');
  }
  return rest;
}

} // namespace bbs
