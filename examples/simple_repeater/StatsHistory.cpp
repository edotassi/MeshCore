#include "StatsHistory.h"

#ifdef WITH_WIFI_DASHBOARD

#if defined(ESP32)
  #include <esp_heap_caps.h>
#endif

void StatsHistory::begin(FILESYSTEM* fs) {
#if defined(ESP32)
  _buf = (HistRecord*) heap_caps_malloc(sizeof(HistRecord) * (size_t)_capacity, MALLOC_CAP_SPIRAM);
  _in_psram = (_buf != NULL);
#endif
  if (_buf == NULL) {
    // fall back to internal RAM if PSRAM isn't available/enabled - still only a few hundred KB
    _buf = (HistRecord*) malloc(sizeof(HistRecord) * (size_t)_capacity);
    _in_psram = false;
  }
  if (_buf == NULL) {
    _capacity = 0;   // out of memory - disable history rather than crash
    return;
  }

  loadCfg(fs);
  loadSnapshot(fs);

  unsigned long now = millis();
  _next_sample_at = now + _sample_interval_secs * 1000UL;
  _next_flush_at = now + _flush_interval_secs * 1000UL;
}

bool StatsHistory::usesPSRAM() const {
  return _in_psram;
}

void StatsHistory::loadCfg(FILESYSTEM* fs) {
  if (!fs->exists(HISTLOG_CFG_FILE)) return;
  File file = fs->open(HISTLOG_CFG_FILE);
  if (file) {
    loadSerial(file);
    file.close();
    if (_sample_interval_secs < HISTLOG_MIN_SAMPLE_INTERVAL_SECS || _sample_interval_secs > HISTLOG_MAX_SAMPLE_INTERVAL_SECS) {
      _sample_interval_secs = HISTLOG_SAMPLE_INTERVAL_SECS;
    }
    if (_flush_interval_secs < HISTLOG_MIN_FLUSH_INTERVAL_SECS || _flush_interval_secs > HISTLOG_MAX_FLUSH_INTERVAL_SECS) {
      _flush_interval_secs = HISTLOG_FLUSH_INTERVAL_SECS;
    }
  }
}

void StatsHistory::saveCfg(FILESYSTEM* fs) {
  File file = fs->open(HISTLOG_CFG_FILE, "w", true);
  if (file) {
    saveSerial(file);
    file.close();
  }
}

void StatsHistory::loadSnapshot(FILESYSTEM* fs) {
  if (_capacity == 0 || !fs->exists(HISTLOG_SNAPSHOT_FILE)) return;

  File file = fs->open(HISTLOG_SNAPSHOT_FILE);
  if (!file) return;

  HistSnapshotHeader hdr;
  if (file.read((uint8_t*)&hdr, sizeof(hdr)) == sizeof(hdr) && hdr.magic == HISTLOG_SNAPSHOT_MAGIC
      && hdr.count <= _capacity && hdr.write_idx < _capacity) {
    // Before the ring has ever wrapped, only the first hdr.count slots were ever written -
    // matches the same "used, not full capacity" sizing flush() writes (see there for why).
    size_t used = (size_t)((hdr.count < _capacity) ? hdr.count : _capacity);
    size_t to_read = sizeof(HistRecord) * used;
    if (file.read((uint8_t*)_buf, to_read) == to_read) {
      _count = hdr.count;
      _write_idx = hdr.write_idx;
      _num_flash_reads++;

      // Pick up the logical clock from where the newest reloaded record left off, rather than
      // implicitly restarting it at 0 (millis() since this boot) - see _boot_offset_secs in the
      // header for why that matters: without this, every reboot would make new samples look
      // older than the old ones already sitting in the buffer, scrambling chart ordering.
      if (_count > 0) {
        _boot_offset_secs = recordAt(_count - 1).uptime_secs + _sample_interval_secs;
      }
    }
  }
  file.close();
}

void StatsHistory::flush(FILESYSTEM* fs) {
  if (_capacity == 0 || !_dirty) return;

  File file = fs->open(HISTLOG_SNAPSHOT_FILE, "w", true);
  if (!file) return;

  HistSnapshotHeader hdr;
  hdr.magic = HISTLOG_SNAPSHOT_MAGIC;
  hdr.count = _count;
  hdr.write_idx = _write_idx;
  file.write((const uint8_t*)&hdr, sizeof(hdr));
  // Only persist the slots actually in use. Before the ring first wraps (_count < _capacity,
  // which - even at a 30s sample interval - is true for the first ~3 days), that's just _count
  // records: a few dozen bytes early on, growing gradually, rather than the full fixed-size
  // buffer (~164KB) on every single flush from the very first one. Once wrapped, the whole
  // buffer genuinely is in use and does need writing in full.
  size_t used = (size_t)((_count < _capacity) ? _count : _capacity);
  file.write((const uint8_t*)_buf, sizeof(HistRecord) * used);
  file.close();

  _dirty = false;
  _num_flash_writes++;
}

void StatsHistory::addSample(uint16_t batt_mv, int16_t rssi_dbm, int16_t snr_x4,
                              int16_t noise_floor_dbm, uint32_t n_recv, uint32_t n_sent,
                              uint32_t n_recv_errors) {
  if (_capacity == 0) return;

  HistRecord& r = _buf[_write_idx];
  r.uptime_secs = nowSecs();
  r.batt_mv = batt_mv;
  r.rssi_dbm = (int8_t)constrain(rssi_dbm, -128, 127);
  r.snr_x4 = (int8_t)constrain(snr_x4, -128, 127);
  r.noise_floor_dbm = (int8_t)constrain(noise_floor_dbm, -128, 127);
  r.n_recv = n_recv;
  r.n_sent = n_sent;
  r.n_recv_errors = (uint16_t)min(n_recv_errors, (uint32_t)0xFFFF);

  _write_idx = (_write_idx + 1) % _capacity;
  if (_count < _capacity) _count++;
  _num_samples_total++;
  _dirty = true;

  _next_sample_at = millis() + _sample_interval_secs * 1000UL;
}

void StatsHistory::maybeFlush(FILESYSTEM* fs) {
  if (_capacity == 0 || !_dirty) return;
  if ((long)(millis() - _next_flush_at) < 0) return;

  flush(fs);
  _next_flush_at = millis() + _flush_interval_secs * 1000UL;
}

bool StatsHistory::setSampleIntervalSecs(uint32_t secs, FILESYSTEM* fs) {
  if (secs < HISTLOG_MIN_SAMPLE_INTERVAL_SECS || secs > HISTLOG_MAX_SAMPLE_INTERVAL_SECS) return false;
  _sample_interval_secs = secs;
  _next_sample_at = millis() + secs * 1000UL;
  saveCfg(fs);
  return true;
}

bool StatsHistory::setFlushIntervalSecs(uint32_t secs, FILESYSTEM* fs) {
  if (secs < HISTLOG_MIN_FLUSH_INTERVAL_SECS || secs > HISTLOG_MAX_FLUSH_INTERVAL_SECS) return false;
  _flush_interval_secs = secs;
  _next_flush_at = millis() + secs * 1000UL;
  saveCfg(fs);
  return true;
}

const HistRecord& StatsHistory::recordAt(uint32_t idx_from_oldest) const {
  uint32_t oldest = (_count < _capacity) ? 0 : _write_idx;
  return _buf[(oldest + idx_from_oldest) % _capacity];
}

int StatsHistory::downsample(int max_points, uint32_t* out_age_secs, float* out_batt_mv,
                              float* out_rssi, float* out_snr, float* out_noise_floor,
                              float* out_recv_rate, float* out_sent_rate,
                              float* out_err_rate) const {
  if (_count == 0 || max_points <= 0) return 0;

  uint32_t now_uptime_secs = nowSecs();
  uint32_t n = _count;
  int n_buckets = (int)min((uint32_t)max_points, n);
  uint32_t per_bucket = (n + n_buckets - 1) / n_buckets;   // ceil

  int out_i = 0;
  uint32_t idx = 0;
  while (idx < n && out_i < n_buckets) {
    uint32_t bucket_end = min(idx + per_bucket, n);
    uint32_t bucket_n = bucket_end - idx;

    double sum_batt = 0, sum_rssi = 0, sum_snr = 0, sum_noise = 0;
    uint32_t sum_age = 0;
    // recv/sent/err are cumulative counters - use first/last of the bucket to derive a rate,
    // rather than averaging the raw cumulative values (which wouldn't mean anything on its own).
    const HistRecord& first = recordAt(idx);
    const HistRecord& last = recordAt(bucket_end - 1);

    for (uint32_t j = idx; j < bucket_end; j++) {
      const HistRecord& r = recordAt(j);
      sum_batt += r.batt_mv;
      sum_rssi += r.rssi_dbm;
      sum_snr += r.snr_x4 / 4.0f;
      sum_noise += r.noise_floor_dbm;
      sum_age += (now_uptime_secs > r.uptime_secs) ? (now_uptime_secs - r.uptime_secs) : 0;
    }

    out_age_secs[out_i] = sum_age / bucket_n;
    out_batt_mv[out_i] = (float)(sum_batt / bucket_n);
    out_rssi[out_i] = (float)(sum_rssi / bucket_n);
    out_snr[out_i] = (float)(sum_snr / bucket_n);
    out_noise_floor[out_i] = (float)(sum_noise / bucket_n);

    uint32_t dt = (last.uptime_secs > first.uptime_secs) ? (last.uptime_secs - first.uptime_secs) : 0;
    if (dt > 0 && bucket_n > 1) {
      out_recv_rate[out_i] = (last.n_recv >= first.n_recv) ? (float)(last.n_recv - first.n_recv) / dt * 3600.0f : 0;
      out_sent_rate[out_i] = (last.n_sent >= first.n_sent) ? (float)(last.n_sent - first.n_sent) / dt * 3600.0f : 0;
      out_err_rate[out_i] = (last.n_recv_errors >= first.n_recv_errors) ? (float)(last.n_recv_errors - first.n_recv_errors) / dt * 3600.0f : 0;
    } else {
      out_recv_rate[out_i] = out_sent_rate[out_i] = out_err_rate[out_i] = 0;
    }

    idx = bucket_end;
    out_i++;
  }
  return out_i;
}

void StatsHistory::resetHistory(FILESYSTEM* fs) {
  _write_idx = 0;
  _count = 0;
  _dirty = false;
  _boot_offset_secs = 0;
  if (fs) fs->remove(HISTLOG_SNAPSHOT_FILE);   // otherwise loadSnapshot() would silently undo this on next boot
}

#endif // WITH_WIFI_DASHBOARD
