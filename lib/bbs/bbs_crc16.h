#pragma once

#include <cstddef>
#include <cstdint>

namespace bbs {

// CRC16-CCITT (polinomio 0x1021, seed 0xFFFF), usato per validare i record
// dei log append-only (post e mail) dopo uno spegnimento improvviso.
uint16_t crc16Ccitt(const uint8_t* data, size_t len);

} // namespace bbs
