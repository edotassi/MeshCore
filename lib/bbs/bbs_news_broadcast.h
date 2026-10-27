#pragma once

#include <cstddef>
#include <cstdint>

#include "bbs_news.h"

namespace bbs {

// Trasmissione delle news sul canale di gruppo: il VPS pubblica fino a 5
// news per volta, ma sul canale radio ne esce una ogni kNewsBroadcastIntervalMs
// per non occupare l'aria. Coda circolare in RAM (non persistita: allo
// spegnimento le news in attesa si perdono, restano comunque nella stanza).
// Parte pura; il canale e la chiave sono risolti nell'adattatore.

constexpr size_t kNewsBroadcastCapacity = 8;
constexpr size_t kNewsBroadcastTextLen = kNewsSourceMaxLen + 3 + kNewsTitleMaxLen + 1;  // "[fonte] titolo" + terminatore
constexpr uint32_t kNewsBroadcastIntervalMs = 30000;

class NewsBroadcastQueue {
public:
  // Accoda "[<fonte>] <titolo>" (troncato su un confine UTF-8 se non entra).
  // Se la coda e' piena scarta la piu' vecchia.
  void push(const char* text);

  // Se la coda non e' vuota e dall'ultimo prelievo sono passati almeno
  // kNewsBroadcastIntervalMs (il primo e' immediato), toglie la piu' vecchia
  // e la copia in 'out' (al piu' cap-1 byte, troncata su un confine UTF-8,
  // sempre terminata) e ritorna true. Il tempo e' in millisecondi con
  // ritorno a zero gestito dalla sottrazione senza segno.
  bool poll(uint32_t now_ms, char* out, size_t cap);

  // Svuota la coda (il distanziamento dall'ultimo invio resta).
  void clear() { _count = 0; _head = 0; }
  size_t size() const { return _count; }

private:
  char _items[kNewsBroadcastCapacity][kNewsBroadcastTextLen] = {};
  size_t _head = 0;   // elemento piu' vecchio
  size_t _count = 0;
  uint32_t _last_ms = 0;
  bool _sent_any = false;
};

} // namespace bbs
