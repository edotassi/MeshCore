#include "WifiDashboard.h"
#include "SvgChart.h"

#if defined(WITH_WIFI_DASHBOARD) && defined(ESP32)

static String row(const char* label, const String& value) {
  return "<tr><td>" + String(label) + "</td><td>" + value + "</td></tr>";
}

static String formatAgo(uint32_t secs) {
  if (secs < 60) return String(secs) + "s ago";
  if (secs < 3600) return String(secs / 60) + "m ago";
  if (secs < 86400) return String(secs / 3600) + "h ago";
  return String(secs / 86400) + "d ago";
}

void WifiDashboard::loadCfg(FILESYSTEM* fs) {
  if (!fs->exists(WIFIDASH_CFG_FILE)) return;
  File file = fs->open(WIFIDASH_CFG_FILE);
  if (file) {
    loadSerial(file);
    file.close();
    if (_timeout_secs < WIFIDASH_MIN_TIMEOUT_SECS || _timeout_secs > WIFIDASH_MAX_TIMEOUT_SECS) {
      _timeout_secs = WIFIDASH_DEFAULT_TIMEOUT_SECS;
    }
  }
}

void WifiDashboard::saveCfg(FILESYSTEM* fs) {
  File file = fs->open(WIFIDASH_CFG_FILE, "w", true);
  if (file) {
    saveSerial(file);
    file.close();
  }
}

void WifiDashboard::begin(DataSource* src, FILESYSTEM* fs) {
  _src = src;
  _fs = fs;
  loadCfg(fs);
}

bool WifiDashboard::start(uint32_t timeout_secs, char reply[]) {
  if (timeout_secs == 0) timeout_secs = _timeout_secs;
  if (timeout_secs < WIFIDASH_MIN_TIMEOUT_SECS || timeout_secs > WIFIDASH_MAX_TIMEOUT_SECS) {
    strcpy(reply, "Err - timeout out of range (1-360 min)");
    return false;
  }

  if (!_active) {
    WiFi.softAP(WIFIDASH_AP_SSID, NULL);   // AP itself is open - the page's Basic Auth (if set) is the gate

    _server = new AsyncWebServer(80);
    _server->on("/", HTTP_GET, [this](AsyncWebServerRequest* request) { handleRoot(request); });
    _server->on("/discover", HTTP_GET, [this](AsyncWebServerRequest* request) { handleDiscover(request); });
    _server->begin();
    _active = true;
  }

  _off_at = millis() + timeout_secs * 1000UL;

  sprintf(reply, "OK - AP up: http://%s (SSID %s, timeout %um)",
          WiFi.softAPIP().toString().c_str(), WIFIDASH_AP_SSID, (unsigned)(timeout_secs / 60));
  return true;
}

void WifiDashboard::stop(char reply[]) {
  if (_active) {
    if (_server) {
      _server->end();
      delete _server;
      _server = NULL;
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    _active = false;
  }
  if (reply) strcpy(reply, "OK - WiFi dashboard off");
}

void WifiDashboard::formatStatusReply(char reply[]) const {
  if (!_active) {
    strcpy(reply, "OK - off");
    return;
  }
  long remain_secs = (long)(_off_at - millis()) / 1000;
  if (remain_secs < 0) remain_secs = 0;
  sprintf(reply, "OK - on: http://%s (%lds left)", WiFi.softAPIP().toString().c_str(), remain_secs);
}

bool WifiDashboard::setPassword(const char* pwd, FILESYSTEM* fs) {
  size_t len = pwd ? strlen(pwd) : 0;
  if (len > 0 && len < 8) return false;   // practical minimum for a Basic Auth password
  if (len >= sizeof(_password)) return false;
  strncpy(_password, pwd ? pwd : "", sizeof(_password) - 1);
  _password[sizeof(_password) - 1] = 0;
  saveCfg(fs);
  return true;
}

bool WifiDashboard::setDefaultTimeoutSecs(uint32_t secs, FILESYSTEM* fs) {
  if (secs < WIFIDASH_MIN_TIMEOUT_SECS || secs > WIFIDASH_MAX_TIMEOUT_SECS) return false;
  _timeout_secs = secs;
  saveCfg(fs);
  return true;
}

void WifiDashboard::loop() {
  if (_active && (long)(millis() - _off_at) >= 0) {
    char discard[8];
    stop(discard);
  }
}

bool WifiDashboard::checkAuth(AsyncWebServerRequest* request) {
  if (_password[0] == 0) return true;   // no password configured
  if (!request->authenticate("admin", _password)) {
    request->requestAuthentication();
    return false;
  }
  return true;
}

void WifiDashboard::handleRoot(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  request->send(200, "text/html", buildPage());
  // an actual page load is as good a signal as the CLI timeout to extend the window a little,
  // but per spec the auto-off is purely time-based from 'wifidash on' - left as-is deliberately
  // so the documented timeout behaviour is predictable.
}

void WifiDashboard::handleDiscover(AsyncWebServerRequest* request) {
  if (!checkAuth(request)) return;
  _src->triggerNeighborDiscovery();   // same mechanism as the 'discover.neighbors' CLI command
  // plain redirect back to the dashboard, no JS - replies trickle in over up to ~60s (see
  // MyMesh::sendNodeDiscoverReq()), so the updated table only shows up on a later reload.
  request->redirect("/");
}

String WifiDashboard::buildNeighboursSection() {
  NeighbourView nbrs[WIFIDASH_MAX_NEIGHBOURS_SHOWN];
  int n = _src->getNeighbours(nbrs, WIFIDASH_MAX_NEIGHBOURS_SHOWN);

  String html = "<div class=\"chart-card\"><h3>Neighbours</h3>";

  html += "<form method=\"GET\" action=\"/discover\"><button type=\"submit\">Discover neighbours</button></form>";
  html += "<p class=\"hint\">Sends a discovery request and returns here immediately - replies can "
          "take up to a minute, reload the page to see them.</p>";

  html += "<table class=\"kv\"><tr><th>#</th><th>ID</th><th>SNR</th><th>Heard</th><th>Location</th></tr>";
  if (n == 0) {
    html += "<tr><td colspan=5>No neighbours heard yet</td></tr>";
  } else {
    for (int i = 0; i < n; i++) {
      html += "<tr><td>" + String(i + 1) + "</td><td>" + String(nbrs[i].id_hex) + "</td><td>" +
              String(nbrs[i].snr_x4 / 4.0f, 1) + " dB</td><td>" + formatAgo(nbrs[i].secs_ago) +
              "</td><td>" + (nbrs[i].has_loc ? (String(nbrs[i].lat, 5) + ", " + String(nbrs[i].lon, 5)) : String("-")) +
              "</td></tr>";
    }
  }
  html += "</table></div>";
  return html;
}

String WifiDashboard::buildSettingsSection() {
  NodePrefs* p = _src->getNodePrefs();
  String html = "<div class=\"chart-card\"><h3>Repeater settings (read-only)</h3>";

  html += "<table class=\"kv\">";
  html += row("Name", p->node_name);
  if (p->owner_info[0]) html += row("Owner info", p->owner_info);
  html += row("Advert interval", String(p->advert_interval * 2) + " min");
  html += row("Flood advert interval", String(p->flood_advert_interval) + " h");
#if ENV_INCLUDE_GPS == 1
  if (p->node_lat != 0.0 || p->node_lon != 0.0) {
    // rounded to ~1.1km precision - never expose the exact repeater site from the dashboard
    html += row("Location (rounded)", String(p->node_lat, 2) + ", " + String(p->node_lon, 2));
  }
#endif
  html += "</table>";

  html += "<table class=\"kv\"><tr><th colspan=2>Radio</th></tr>";
  html += row("Frequency", String(p->freq, 3) + " MHz");
  html += row("Bandwidth", String(p->bw, 1) + " kHz");
  html += row("Spreading factor", String(p->sf));
  html += row("Coding rate", String(p->cr));
  html += row("TX power", String(p->tx_power_dbm) + " dBm");
  html += row("CAD enabled", p->cad_enabled ? "yes" : "no");
  html += row("Interference threshold", String(p->interference_threshold));
  html += row("RX boosted gain", p->rx_boosted_gain ? "yes" : "no");
  html += row("Airtime factor", String(p->airtime_factor, 2));
  html += row("RX delay base", String(p->rx_delay_base, 2));
  html += row("TX delay factor (flood)", String(p->tx_delay_factor, 2));
  html += row("TX delay factor (direct)", String(p->direct_tx_delay_factor, 2));
  html += row("AGC reset interval", String((int)p->agc_reset_interval * 4) + " s");
  html += row("Path hash mode", String(p->path_hash_mode));
  html += row("Multi-ACK count", String(p->multi_acks));
  html += "</table>";

#if defined(WITH_BRIDGE)
  html += "<table class=\"kv\"><tr><th colspan=2>Bridge</th></tr>";
  html += row("Enabled", p->bridge_enabled ? "yes" : "no");
  html += row("Delay", String(p->bridge_delay) + " ms");
  html += row("Packet source", p->bridge_pkt_src == 0 ? "logTx" : "logRx");
  html += row("Baud", String(p->bridge_baud));
  html += row("Channel (ESP-NOW)", String(p->bridge_channel));
  html += "</table>";
  // bridge_secret is deliberately never rendered
#endif

#if ENV_INCLUDE_GPS == 1
  html += "<table class=\"kv\"><tr><th colspan=2>GPS</th></tr>";
  html += row("Enabled", p->gps_enabled ? "yes" : "no");
  html += row("Interval", String(p->gps_interval) + " s");
  html += row("Advert location policy", String(p->advert_loc_policy));
  html += "</table>";
#endif

  html += "<table class=\"kv\"><tr><th colspan=2>Repeat</th></tr>";
  html += row("Forwarding disabled", p->disable_fwd ? "yes" : "no");
  html += row("Flood max hops", String(p->flood_max));
  html += row("Flood max (unscoped)", String(p->flood_max_unscoped));
  html += row("Flood max (advert)", String(p->flood_max_advert));
  html += row("Loop detect", String(p->loop_detect));
  html += "</table>";

  html += "<table class=\"kv\"><tr><th colspan=2>Power</th></tr>";
  html += row("ADC multiplier", String(p->adc_multiplier, 3));
  html += row("Power-saving enabled", p->powersaving_enabled ? "yes" : "no");
  html += "</table>";

  // password / guest_password / bridge_secret intentionally never read here.
  html += "</div>";
  return html;
}

String WifiDashboard::buildChartsSection() {
  StatsHistory* h = _src->getStatsHistory();

  static uint32_t age[WIFIDASH_CHART_POINTS];
  static float batt[WIFIDASH_CHART_POINTS], rssi[WIFIDASH_CHART_POINTS], snr[WIFIDASH_CHART_POINTS],
               noise[WIFIDASH_CHART_POINTS], recv_rate[WIFIDASH_CHART_POINTS],
               sent_rate[WIFIDASH_CHART_POINTS], err_rate[WIFIDASH_CHART_POINTS];

  int n = h->downsample(WIFIDASH_CHART_POINTS, age, batt, rssi, snr, noise,
                         recv_rate, sent_rate, err_rate);

  String html = "<div class=\"chart-card\"><h3>Battery</h3>";
  SvgChart::Options bopt;
  bopt.stroke_color = "#2e7d32";
  bopt.grad_top = "#2e7d3255";
  bopt.grad_bottom = "#2e7d3200";
  bopt.value_suffix = " mV";
  html += SvgChart::render(age, batt, n, "batt", bopt);
  html += "</div>";

  html += "<div class=\"chart-card\"><h3>RSSI / SNR / noise floor (dBm / dB)</h3>";
  SvgChart::Options ropt;
  ropt.stroke_color = "#1565c0";
  ropt.grad_top = "#1565c055";
  ropt.grad_bottom = "#1565c000";
  ropt.value_suffix = " dBm";
  html += "<div class=\"chart-label\">RSSI</div>" + SvgChart::render(age, rssi, n, "rssi", ropt);

  SvgChart::Options sopt;
  sopt.stroke_color = "#6a1b9a";
  sopt.grad_top = "#6a1b9a55";
  sopt.grad_bottom = "#6a1b9a00";
  sopt.value_suffix = " dB";
  html += "<div class=\"chart-label\">SNR</div>" + SvgChart::render(age, snr, n, "snr", sopt);

  SvgChart::Options nopt;
  nopt.stroke_color = "#ef6c00";
  nopt.grad_top = "#ef6c0055";
  nopt.grad_bottom = "#ef6c0000";
  nopt.value_suffix = " dBm";
  html += "<div class=\"chart-label\">Noise floor</div>" + SvgChart::render(age, noise, n, "noise", nopt);
  html += "</div>";

  html += "<div class=\"chart-card\"><h3>Packet rates (per hour)</h3>";
  SvgChart::Options rrateopt;
  rrateopt.stroke_color = "#00838f";
  rrateopt.grad_top = "#00838f55";
  rrateopt.grad_bottom = "#00838f00";
  rrateopt.value_suffix = "/h";
  rrateopt.decimals = 1;
  html += "<div class=\"chart-label\">Received</div>" + SvgChart::render(age, recv_rate, n, "recv", rrateopt);

  SvgChart::Options srateopt = rrateopt;
  srateopt.stroke_color = "#2e7d32";
  srateopt.grad_top = "#2e7d3255";
  srateopt.grad_bottom = "#2e7d3200";
  html += "<div class=\"chart-label\">Sent</div>" + SvgChart::render(age, sent_rate, n, "sent", srateopt);

  SvgChart::Options erateopt = rrateopt;
  erateopt.stroke_color = "#c62828";
  erateopt.grad_top = "#c6282855";
  erateopt.grad_bottom = "#c6282800";
  html += "<div class=\"chart-label\">RX errors</div>" + SvgChart::render(age, err_rate, n, "err", erateopt);
  html += "</div>";

  return html;
}

String WifiDashboard::buildPage() {
  RepeaterStats stats;
  _src->getRepeaterStats(stats);
  StatsHistory* h = _src->getStatsHistory();
  // deliberately NOT flushing here: the chart data below is read straight from the PSRAM ring
  // buffer regardless of flush state, so a flush wouldn't change what's displayed - and
  // StatsHistory::flush() writes the *entire* ~164KB buffer synchronously to SPIFFS (not just
  // the new samples), which is exactly the kind of large blocking flash write this feature is
  // supposed to avoid, doubly so from inside an AsyncWebServer request handler. Periodic
  // flushing from MyMesh::loop() (every flush interval) is the only place this should happen.

  float batt_pct = (float)(stats.batt_milli_volts - 3000) * 100.0f / (4200 - 3000);
  if (batt_pct < 0) batt_pct = 0;
  if (batt_pct > 100) batt_pct = 100;

  String html;
  html.reserve(24576);

  html += "<!doctype html><html><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  html += "<title>" + String(_src->getNodeName()) + " - MeshCore dashboard</title>";
  html += "<style>";
  html += "body{font-family:system-ui,sans-serif;margin:0;padding:12px;background:#fafafa;color:#222}";
  html += "h1{font-size:18px;margin:4px 0 12px}";
  html += ".kv{width:100%;border-collapse:collapse;font-size:13px;margin-bottom:6px}";
  html += ".kv td,.kv th{padding:3px 6px;text-align:left;border-bottom:1px solid #eee}";
  html += ".kv th{background:#f0f0f0;font-size:11px;text-transform:uppercase;color:#666}";
  html += ".chart-label{font:600 11px system-ui,sans-serif;color:#666;margin-top:6px}";
  // Current status + Settings sit stacked (mobile) or side by side as two distinct cards once
  // there's room (desktop) - each keeps its own <div class="chart-card">, only the row wrapper's
  // layout direction changes.
  html += ".row-2col{display:flex;flex-direction:column;gap:12px}";
  html += ".row-2col>.chart-card{margin:0}";
  html += "@media (min-width:700px){.row-2col{flex-direction:row;align-items:flex-start}.row-2col>.chart-card{flex:1;min-width:0}}";
  html += ".hint{font-size:11px;color:#888;margin:4px 0}";
  html += "button{padding:6px 14px;font-size:13px;border:1px solid #ccc;border-radius:6px;background:#fff;cursor:pointer}";
  html += SvgChart::sharedStyle();
  html += "</style></head><body>";

  html += "<h1>" + String(_src->getNodeName()) + " &mdash; MeshCore repeater dashboard</h1>";

  html += "<div class=\"row-2col\">";
  html += "<div class=\"chart-card\"><h3>Current status</h3><table class=\"kv\">";
  html += row("Battery", String(stats.batt_milli_volts) + " mV (~" + String(batt_pct, 0) + "%)");
  html += row("Last RSSI / SNR / noise floor", String(stats.last_rssi) + " dBm / " +
              String(stats.last_snr / 4.0f, 1) + " dB / " + String(stats.noise_floor) + " dBm");
  html += row("Packets recv / sent", String(stats.n_packets_recv) + " / " + String(stats.n_packets_sent));
  html += row("Flood sent / direct sent", String(stats.n_sent_flood) + " / " + String(stats.n_sent_direct));
  html += row("Flood recv / direct recv", String(stats.n_recv_flood) + " / " + String(stats.n_recv_direct));
  html += row("Duplicates filtered (direct/flood)", String(stats.n_direct_dups) + " / " + String(stats.n_flood_dups));
  html += row("RX errors", String(stats.n_recv_errors));
  html += row("TX queue length", String(stats.curr_tx_queue_len));
  html += row("Air time (TX/RX)", String(stats.total_air_time_secs) + "s / " + String(stats.total_rx_air_time_secs) + "s");
  html += row("Uptime", String(stats.total_up_time_secs / 3600) + "h " + String((stats.total_up_time_secs / 60) % 60) + "m");
  html += row("Free heap", String(ESP.getFreeHeap() / 1024) + " KB");
  html += row("MCU temperature", String(_src->getMCUTempC(), 1) + " &deg;C");
  html += row("Last reset reason", _src->getResetReasonStr());
  html += row("History storage", String(h->count()) + " / " + String(h->capacity()) + " samples (" +
              (h->usesPSRAM() ? "PSRAM" : "internal RAM") + "), " + String(h->getNumFlashWrites()) + " flash writes so far");
  html += row("Firmware", String(_src->getFirmwareVer()) + " (" + String(_src->getBuildDate()) + ")");
  html += "</table></div>";

  html += buildSettingsSection();
  html += "</div>"; // .row-2col

  html += buildNeighboursSection();
  html += buildChartsSection();

  html += "</body></html>";
  return html;
}

#endif // defined(WITH_WIFI_DASHBOARD) && defined(ESP32)
