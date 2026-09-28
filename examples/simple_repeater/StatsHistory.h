#pragma once

#include <Arduino.h>   // needed for PlatformIO

// Self-guarded the same way ESPNowBridge.h/WITH_ESPNOW_BRIDGE is: this file's build_src_filter
// entry (examples/simple_repeater's directory wildcard) is shared with the default
// heltec_v4_repeater env, so the flag has to gate the file's content, not just its includer -
// otherwise this would compile (unused, but present) into every default repeater build too.
#ifdef WITH_WIFI_DASHBOARD

#include <helpers/ConfigSerializer.h>
#include <helpers/IdentityStore.h>   // defines the FILESYSTEM type

// ESP32-only feature - safe to assume fs::FS/File (SPIFFS) here rather than the cross-platform
// FILESYSTEM guards used elsewhere in this codebase.
using File = fs::File;

#ifndef HISTLOG_MAX_RECORDS
  #define HISTLOG_MAX_RECORDS   8640   // ~30 days of history @ 5 min sampling (19 bytes/record => ~164KB in PSRAM)
#endif

#ifndef HISTLOG_SAMPLE_INTERVAL_SECS
  #define HISTLOG_SAMPLE_INTERVAL_SECS   300   // 5 minutes
#endif

#ifndef HISTLOG_FLUSH_INTERVAL_SECS
  #define HISTLOG_FLUSH_INTERVAL_SECS   1800   // 30 minutes
#endif

#define HISTLOG_MIN_SAMPLE_INTERVAL_SECS   30
#define HISTLOG_MAX_SAMPLE_INTERVAL_SECS   3600
#define HISTLOG_MIN_FLUSH_INTERVAL_SECS    60
#define HISTLOG_MAX_FLUSH_INTERVAL_SECS    (24UL * 3600UL)

#define HISTLOG_SNAPSHOT_FILE   "/histlog"
#define HISTLOG_CFG_FILE        "/histlog_cfg"
#define HISTLOG_SNAPSHOT_MAGIC  0x48534C31UL   // "HSL1"

// One compact sample. Cumulative counters (n_recv/n_sent/n_recv_errors) are snapshotted as-is
// from RepeaterStats at sample time - the dashboard derives per-interval deltas by diffing
// consecutive records when charting, so nothing needs to be reset between samples.
struct __attribute__((packed)) HistRecord {
  uint32_t uptime_secs;
  uint16_t batt_mv;
  int8_t   rssi_dbm;
  int8_t   snr_x4;        // SNR * 4, same scale as RepeaterStats.last_snr
  int8_t   noise_floor_dbm;
  uint32_t n_recv;
  uint32_t n_sent;
  uint16_t n_recv_errors;
};

struct __attribute__((packed)) HistSnapshotHeader {
  uint32_t magic;
  uint32_t count;
  uint32_t write_idx;
};

/**
 * \brief  Repeater-only feature (behind WITH_WIFI_DASHBOARD): keeps a fixed-capacity ring
 *     buffer of HistRecord samples resident in PSRAM (capacity sized so the whole buffer is a
 *     couple hundred KB - trivial next to the 2MB PSRAM on Heltec V4), sampled periodically and
 *     periodically snapshotted to a single SPIFFS file as one batched write, so normal operation
 *     costs zero flash writes between flush intervals. Because the full ring lives in RAM, reads
 *     (for the dashboard page) are always up to date - there's no separate "flash vs still-
 *     buffered" merge needed at read time, only at boot (to restore across a reboot) and at
 *     flush time (to persist).
 */
class StatsHistory : public ConfigSerializer {
  HistRecord* _buf = NULL;         // PSRAM-resident when available, capacity HISTLOG_MAX_RECORDS
  uint32_t _capacity = HISTLOG_MAX_RECORDS;
  uint32_t _write_idx = 0;
  uint32_t _count = 0;
  bool _dirty = false;
  bool _in_psram = false;

  uint32_t _sample_interval_secs = HISTLOG_SAMPLE_INTERVAL_SECS;
  uint32_t _flush_interval_secs = HISTLOG_FLUSH_INTERVAL_SECS;
  unsigned long _next_sample_at = 0;
  unsigned long _next_flush_at = 0;

  // uptime_secs in HistRecord must keep increasing across a reboot (the ring buffer is reloaded
  // from flash, so old pre-reboot records sit alongside new ones) - but millis() always restarts
  // at 0 on boot. _boot_offset_secs is added to millis()/1000 to produce a logical clock that
  // picks up counting from wherever the last-persisted record left off, instead of both old and
  // new records claiming to be "around time zero" and scrambling chronological order. See
  // nowSecs(), and loadSnapshot() where this gets set from the reloaded data.
  uint32_t _boot_offset_secs = 0;

  uint32_t _num_samples_total = 0;
  uint32_t _num_flash_writes = 0;
  uint32_t _num_flash_reads = 0;

  void loadCfg(FILESYSTEM* fs);
  void saveCfg(FILESYSTEM* fs);
  void loadSnapshot(FILESYSTEM* fs);
  uint32_t nowSecs() const { return _boot_offset_secs + millis() / 1000; }

protected:
  void structure() override {
    def("smp_int", _sample_interval_secs);
    def("flush_int", _flush_interval_secs);
  }

public:
  void begin(FILESYSTEM* fs);

  // Cheap to call every MyMesh::loop() tick (just a millis() comparison) - lets the caller avoid
  // gathering a fresh RepeaterStats snapshot (which does live ADC/radio reads) except on the
  // sample interval itself, rather than paying that cost on every loop() iteration.
  bool isSampleDue() const { return (long)(millis() - _next_sample_at) >= 0; }

  // Call only when isSampleDue() is true. Appends into the PSRAM ring buffer and schedules the
  // next sample - does not touch flash (see maybeFlush()). Timestamps itself via nowSecs()
  // rather than taking uptime as a parameter, so it's never at the mercy of a caller handing in
  // a value that resets across reboots.
  void addSample(uint16_t batt_mv, int16_t rssi_dbm, int16_t snr_x4, int16_t noise_floor_dbm,
                 uint32_t n_recv, uint32_t n_sent, uint32_t n_recv_errors);

  // Cheap to call every MyMesh::loop() tick. Flushes to flash only once the flush interval has
  // elapsed AND there's something unflushed - this is the single batched write per interval that
  // keeps flash wear negligible (see the design notes: no per-sample flash I/O).
  void maybeFlush(FILESYSTEM* fs);

  // Force an immediate snapshot write (e.g. when the dashboard page is about to be served, so
  // a reboot right after doesn't lose the last few unflushed samples). No-op if nothing changed
  // since the last flush.
  void flush(FILESYSTEM* fs);

  bool setSampleIntervalSecs(uint32_t secs, FILESYSTEM* fs);
  bool setFlushIntervalSecs(uint32_t secs, FILESYSTEM* fs);
  uint32_t getSampleIntervalSecs() const { return _sample_interval_secs; }
  uint32_t getFlushIntervalSecs() const { return _flush_interval_secs; }

  uint32_t count() const { return _count; }
  uint32_t capacity() const { return _capacity; }
  bool usesPSRAM() const;

  // idx_from_oldest: 0 = oldest record still retained, count()-1 = newest.
  const HistRecord& recordAt(uint32_t idx_from_oldest) const;

  // Block-averages the full retained history down to at most max_points buckets (per the
  // dashboard's downsampling requirement). out_* arrays must each be caller-allocated with at
  // least max_points entries. out_age_secs[i] is how many seconds ago (relative to nowSecs())
  // that bucket's samples were centred on. Returns the number of buckets actually written (<=
  // max_points; 0 if there's no history yet).
  int downsample(int max_points, uint32_t* out_age_secs, float* out_batt_mv, float* out_rssi,
                  float* out_snr, float* out_noise_floor, float* out_recv_rate,
                  float* out_sent_rate, float* out_err_rate) const;

  // Clears samples (config untouched) - also removes the on-flash snapshot and resets the
  // logical clock, so a stale copy doesn't get silently reloaded on the next boot.
  void resetHistory(FILESYSTEM* fs);

  uint32_t getNumSamplesTotal() const { return _num_samples_total; }
  uint32_t getNumFlashWrites() const { return _num_flash_writes; }
  uint32_t getNumFlashReads() const { return _num_flash_reads; }
};

#endif // WITH_WIFI_DASHBOARD
