#pragma once

#include <Arduino.h>   // needed for PlatformIO

// Self-guarded the same way ESPNowBridge.h/WITH_ESPNOW_BRIDGE is: this file's build_src_filter
// entry (examples/simple_repeater's directory wildcard) is shared with the default
// heltec_v4_repeater env, so the flag has to gate the file's content, not just its includer.
#ifdef WITH_WIFI_DASHBOARD

#include <helpers/ConfigSerializer.h>
#include <helpers/IdentityStore.h>   // defines the FILESYSTEM type
#include <helpers/CommonCLI.h>       // NodePrefs
#include "RepeaterStats.h"
#include "StatsHistory.h"

// ESP32-only feature, same as ESP32Board.cpp's equivalent WiFi-AP-on-demand feature (startOTAUpdate).
#if defined(ESP32)
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#endif

#ifndef WIFIDASH_DEFAULT_TIMEOUT_SECS
  #define WIFIDASH_DEFAULT_TIMEOUT_SECS   600U   // 10 minutes
#endif
#define WIFIDASH_MIN_TIMEOUT_SECS   60U
#define WIFIDASH_MAX_TIMEOUT_SECS   (6UL * 3600UL)
#define WIFIDASH_CFG_FILE           "/wifidash_cfg"
#define WIFIDASH_AP_SSID            "MeshCore-Dash"
// Bounds worst-case page size (7 charts on one page, built as a single in-RAM String on a
// heap-constrained ESP32) regardless of how much raw history has accumulated - see the circle
// count cap in SvgChart::render() for the other half of this budget.
#define WIFIDASH_CHART_POINTS       60   // must be <= SVGCHART_MAX_POINTS

/**
 * \brief  Repeater-only feature (behind WITH_WIFI_DASHBOARD): WiFi is off by default (battery/
 *     solar constrained device) and is only switched on for a bounded window via the
 *     'wifidash on/off/status' CLI commands, serving a single self-contained HTML status page
 *     (current stats + read-only settings + inline-SVG history charts, no external JS/CSS since
 *     there's no internet access from the AP). Mirrors ESP32Board::startOTAUpdate()'s
 *     WiFi.softAP()+AsyncWebServer shape.
 */
#ifndef WIFIDASH_MAX_NEIGHBOURS_SHOWN
  #define WIFIDASH_MAX_NEIGHBOURS_SHOWN   20   // table row cap - map markers are further capped to 9 (single-char label limit)
#endif

// One row of the neighbours table/map - a read-only projection of NeighbourInfo, kept separate
// so WifiDashboard doesn't need to know about MyMesh's private neighbours[] storage directly.
struct NeighbourView {
  char id_hex[9];       // 4 bytes of pubkey as hex, same convention as formatNeighborsReply()
  int8_t snr_x4;
  uint32_t secs_ago;
  bool has_loc;
  double lat, lon;
};

class WifiDashboard : public ConfigSerializer {
public:
  // Decouples this class from MyMesh, the same way CommonCLICallbacks decouples CommonCLI -
  // MyMesh implements this interface alongside CommonCLICallbacks rather than WifiDashboard
  // holding a back-reference to MyMesh.
  struct DataSource {
    virtual void getRepeaterStats(RepeaterStats& out) = 0;
    virtual NodePrefs* getNodePrefs() = 0;
    virtual StatsHistory* getStatsHistory() = 0;
    virtual const char* getNodeName() = 0;
    virtual const char* getFirmwareVer() = 0;
    virtual const char* getBuildDate() = 0;
    virtual float getMCUTempC() = 0;
    virtual const char* getResetReasonStr() = 0;
    // Fills out[] (caller-allocated, max_out entries) with known neighbours, newest-heard first.
    // Returns the number written.
    virtual int getNeighbours(NeighbourView* out, int max_out) = 0;
    // Same mechanism as the existing 'discover.neighbors' CLI command (sendNodeDiscoverReq()) -
    // replies trickle in over up to ~60s, so the dashboard button just triggers it and the
    // updated list shows up on a later page reload, not instantly.
    virtual void triggerNeighborDiscovery() = 0;
  };

private:
  DataSource* _src = NULL;
  FILESYSTEM* _fs = NULL;
#if defined(ESP32)
  AsyncWebServer* _server = NULL;
#endif
  bool _active = false;
  unsigned long _off_at = 0;
  uint32_t _timeout_secs = WIFIDASH_DEFAULT_TIMEOUT_SECS;
  char _password[32] = { 0 };   // HTTP Basic Auth for the page; empty = open (AP itself has no WPA2 password)

  void loadCfg(FILESYSTEM* fs);
  void saveCfg(FILESYSTEM* fs);

#if defined(ESP32)
  bool checkAuth(AsyncWebServerRequest* request);
  void handleRoot(AsyncWebServerRequest* request);
  void handleDiscover(AsyncWebServerRequest* request);
#endif
  String buildNeighboursSection();
  String buildPage();
  String buildSettingsSection();
  String buildChartsSection();

protected:
  void structure() override {
    def("timeout", _timeout_secs);
    def("pass", _password, sizeof(_password));
  }

public:
  void begin(DataSource* src, FILESYSTEM* fs);

  // "wifidash on [timeout_min]" - timeout_min == 0 means "use the persisted default".
  bool start(uint32_t timeout_secs, char reply[]);
  // "wifidash off"
  void stop(char reply[]);
  // "wifidash status"
  void formatStatusReply(char reply[]) const;

  bool setPassword(const char* pwd, FILESYSTEM* fs);
  bool setDefaultTimeoutSecs(uint32_t secs, FILESYSTEM* fs);

  void loop();   // auto-off timeout check, called every MyMesh::loop() tick

  bool isActive() const { return _active; }
};

#endif // WITH_WIFI_DASHBOARD
