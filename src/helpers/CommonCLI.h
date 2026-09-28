#pragma once

#include "Mesh.h"
#include <helpers/IdentityStore.h>
#include <helpers/SensorManager.h>
#include <helpers/ClientACL.h>
#include <helpers/RegionMap.h>
#include <helpers/ConfigSerializer.h>

#if defined(WITH_RS232_BRIDGE) || defined(WITH_ESPNOW_BRIDGE) || defined(WITH_MQTT_BRIDGE)
#define WITH_BRIDGE
#endif

#define ADVERT_LOC_NONE       0
#define ADVERT_LOC_SHARE      1
#define ADVERT_LOC_PREFS      2

#define LOOP_DETECT_OFF       0
#define LOOP_DETECT_MINIMAL   1
#define LOOP_DETECT_MODERATE  2
#define LOOP_DETECT_STRICT    3

class NodePrefs : public ConfigSerializer {
public:
  // in-memory backing data
  float airtime_factor = 0;
  char node_name[32];
  double node_lat = 0, node_lon = 0;
  char password[16];
  float freq = 0;
  int8_t tx_power_dbm = 0;
  uint8_t disable_fwd = 0;
  uint8_t advert_interval = 0;       // minutes / 2
  uint8_t flood_advert_interval = 0; // hours
  float rx_delay_base = 0;
  float tx_delay_factor = 0;
  char guest_password[16];
  float direct_tx_delay_factor = 0;
  uint32_t guard;
  uint8_t sf = 0;
  uint8_t cr = 0;
  uint8_t allow_read_only = 0;
  uint8_t multi_acks = 0;
  float bw = 0;
  uint8_t flood_max = 0;
  uint8_t flood_max_unscoped = 0;
  uint8_t flood_max_advert = 0;
  uint8_t interference_threshold = 0;
  uint8_t agc_reset_interval = 0; // secs / 4
  // Bridge settings
  uint8_t bridge_enabled = 0; // boolean
  uint16_t bridge_delay = 0;  // milliseconds (default 500 ms)
  uint8_t bridge_pkt_src = 0; // 0 = logTx, 1 = logRx (default logTx)
  uint32_t bridge_baud = 0;   // 9600, 19200, 38400, 57600, 115200 (default 115200)
  uint8_t bridge_channel = 0; // 1-14 (ESP-NOW only)
  char bridge_secret[16]; // for XOR encryption of bridge packets (ESP-NOW only)
  // MQTT bridge settings (MQTT only)
  char wifi_ssid[33];
  char wifi_pwd[64];
  char mqtt_server[64];
  uint16_t mqtt_port = 1883;
  char mqtt_username[32];
  char mqtt_password[32];
  char mqtt_origin[32];
  char mqtt_iata[8];
  uint8_t mqtt_status_enabled = 0;
  uint8_t mqtt_tx_enabled = 0;
  uint8_t mqtt_raw_enabled = 0;
  uint32_t mqtt_interval = 60000;
  // Wormhole bridge settings (independent MQTT connection, separate broker from mqtt_* above)
  uint8_t wormhole_enabled = 0;
  char wormhole_server[64];
  uint16_t wormhole_port = 1883;
  char wormhole_username[32];
  char wormhole_password[32];
  char wormhole_pub_topic[64];
  char wormhole_sub_topic[64];
  // Power setting
  uint8_t powersaving_enabled = 0; // boolean
  // Gps settings
  uint8_t gps_enabled = 0;
  uint32_t gps_interval = 0; // in seconds
  uint8_t advert_loc_policy = 0;
  uint32_t discovery_mod_timestamp = 0;
  float adc_multiplier = 0;
  char owner_info[120];
  uint8_t rx_boosted_gain = 0; // power settings
  uint8_t radio_fem_rxgain = 0; // LoRa FEM RX gain setting
  uint8_t radio_fem_txgain = 0; // LoRa FEM TX gain setting
  uint8_t path_hash_mode = 0;   // which path mode to use when sending
  uint8_t loop_detect = 0;
  uint8_t cad_enabled = 0;      // hardware Channel Activity Detection before TX (boolean)
  uint8_t extra_sf[4];
#ifdef WITH_MQTT_BRIDGE
  uint8_t screen_timeout_enabled = 0;  // default off: preserves pre-existing "always on" screen behaviour on MQTT/WiFi-dashboard builds
#else
  uint8_t screen_timeout_enabled = 1;  // default on: preserves pre-existing 20s auto-off behaviour on other builds
#endif

private:
  class RadioPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("freq", _parent->freq);
      def("bw", _parent->bw);
      def("sf", _parent->sf);
      def("cr", _parent->cr);
      def("cad", _parent->cad_enabled);
      def("int_thr", _parent->interference_threshold);
      def("rxgain", _parent->rx_boosted_gain);
      def("fem_rxgain", _parent->radio_fem_rxgain);
      def("fem_txgain", _parent->radio_fem_txgain);
      def("tx", _parent->tx_power_dbm);
      def("af", _parent->airtime_factor);
      def("rxdelay", _parent->rx_delay_base);
      def("f_txdelay", _parent->tx_delay_factor);
      def("d_txdelay", _parent->direct_tx_delay_factor);
      def("agc_int", _parent->agc_reset_interval);
      def("hash_mode", _parent->path_hash_mode);
      def("multi_ack", _parent->multi_acks);
    }
  public:
    RadioPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  RadioPrefs radio;

  class BridgePrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("en", _parent->bridge_enabled); // boolean
      def("delay", _parent->bridge_delay);  // milliseconds (default 500 ms)
      def("src", _parent->bridge_pkt_src); // 0 = logTx, 1 = logRx (default logTx)
      def("baud", _parent->bridge_baud);   // 9600, 19200, 38400, 57600, 115200 (default 115200)
      def("ch", _parent->bridge_channel); // 1-14 (ESP-NOW only)
      def("secret", _parent->bridge_secret, sizeof(_parent->bridge_secret)); // for XOR encryption of bridge packets (ESP-NOW only)
    }
  public:
    BridgePrefs(NodePrefs* parent) : _parent(parent) { }
  };
  BridgePrefs bridge;

  class GPSPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("en", _parent->gps_enabled); // boolean
      def("int", _parent->gps_interval);   // interval in seconds
      def("adv_loc", _parent->advert_loc_policy);
    }
  public:
    GPSPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  GPSPrefs gps;

  class PowerPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("adc_mult", _parent->adc_multiplier);
      def("pwr_sav_en", _parent->powersaving_enabled);
    }
  public:
    PowerPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  PowerPrefs power;

  class RepeatPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("disable", _parent->disable_fwd);
      def("f_max", _parent->flood_max);
      def("f_max_uns", _parent->flood_max_unscoped);
      def("f_max_adv", _parent->flood_max_advert);
      def("loop", _parent->loop_detect);
    }
  public:
    RepeatPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  RepeatPrefs repeat;

  class RoomPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("rd_only", _parent->allow_read_only);
    }
  public:
    RoomPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  RoomPrefs room;

  class MqttPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("wifi_ssid", _parent->wifi_ssid, sizeof(_parent->wifi_ssid));
      def("wifi_pwd", _parent->wifi_pwd, sizeof(_parent->wifi_pwd));
      def("srv", _parent->mqtt_server, sizeof(_parent->mqtt_server));
      def("port", _parent->mqtt_port);
      def("user", _parent->mqtt_username, sizeof(_parent->mqtt_username));
      def("pass", _parent->mqtt_password, sizeof(_parent->mqtt_password));
      def("origin", _parent->mqtt_origin, sizeof(_parent->mqtt_origin));
      def("iata", _parent->mqtt_iata, sizeof(_parent->mqtt_iata));
      def("status", _parent->mqtt_status_enabled);
      def("tx", _parent->mqtt_tx_enabled);
      def("raw", _parent->mqtt_raw_enabled);
      def("interval", _parent->mqtt_interval);
    }
  public:
    MqttPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  MqttPrefs mqtt;

  class WormholePrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("en", _parent->wormhole_enabled);
      def("srv", _parent->wormhole_server, sizeof(_parent->wormhole_server));
      def("port", _parent->wormhole_port);
      def("user", _parent->wormhole_username, sizeof(_parent->wormhole_username));
      def("pass", _parent->wormhole_password, sizeof(_parent->wormhole_password));
      def("pub", _parent->wormhole_pub_topic, sizeof(_parent->wormhole_pub_topic));
      def("sub", _parent->wormhole_sub_topic, sizeof(_parent->wormhole_sub_topic));
    }
  public:
    WormholePrefs(NodePrefs* parent) : _parent(parent) { }
  };
  WormholePrefs wormhole;

protected:
  void structure() override {
    def("name", node_name, sizeof(node_name));
    def("pass", password, sizeof(password));
    def("guest", guest_password, sizeof(guest_password));
    def("owner", owner_info, sizeof(owner_info));
    def("adv_int", advert_interval);
    def("f_adv_int", flood_advert_interval);
    def("scr_to", screen_timeout_enabled);
    def("lat", node_lat);
    def("lon", node_lon);
    def("radio", radio);
    def("bridge", bridge);
    def("gps", gps);
    def("repeat", repeat);
    def("room", room);
    def("power", power);
    def("mqtt", mqtt);
    def("wormhole", wormhole);
  }

public:
  NodePrefs() : ConfigSerializer(), bridge(this), gps(this), radio(this), power(this), repeat(this), room(this), mqtt(this), wormhole(this) {
    node_name[0] = 0;
    password[0] = 0;
    guest_password[0] = 0;
    bridge_secret[0] = 0;
    owner_info[0] = 0;
    wifi_ssid[0] = 0;
    wifi_pwd[0] = 0;
    mqtt_server[0] = 0;
    mqtt_username[0] = 0;
    mqtt_password[0] = 0;
    mqtt_origin[0] = 0;
    mqtt_iata[0] = 0;
    wormhole_server[0] = 0;
    wormhole_username[0] = 0;
    wormhole_password[0] = 0;
    wormhole_pub_topic[0] = 0;
    wormhole_sub_topic[0] = 0;
  }
};

class CommonCLICallbacks {
public:
  virtual void savePrefs() = 0;
  virtual const char* getFirmwareVer() = 0;
  virtual const char* getBuildDate() = 0;
  virtual const char* getRole() = 0;
  virtual bool formatFileSystem() = 0;
  virtual void sendSelfAdvertisement(int delay_millis, bool flood) = 0;
  virtual void updateAdvertTimer() = 0;
  virtual void updateFloodAdvertTimer() = 0;
  virtual void setLoggingOn(bool enable) = 0;
  virtual void eraseLogFile() = 0;
  virtual void dumpLogFile() = 0;
  virtual void setTxPower(int8_t power_dbm) = 0;
  virtual void formatNeighborsReply(char *reply) = 0;
  virtual void removeNeighbor(const uint8_t* pubkey, int key_len) {
    // no op by default
  };
  virtual void formatStatsReply(char *reply) = 0;
  virtual void formatRadioStatsReply(char *reply) = 0;
  virtual void formatPacketStatsReply(char *reply) = 0;
  virtual mesh::LocalIdentity& getSelfId() = 0;
  virtual void saveIdentity(const mesh::LocalIdentity& new_id) = 0;
  virtual void clearStats() = 0;
  virtual void applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) = 0;

  virtual void startRegionsLoad() {
    // no op by default
  }
  virtual bool saveRegions() {
    return false;
  }
  virtual void onDefaultRegionChanged(const RegionEntry* r) {
    // no op by default
  }

  virtual void setBridgeState(bool enable) {
    // no op by default
  };

  virtual void setWormholeState(bool enable) {
    // no op by default
  };

  virtual bool isWormholeRunning() { return false; }
  virtual bool isWormholeConnected() { return false; }
  virtual uint32_t getWormholeSentCount() { return 0; }
  virtual uint32_t getWormholeReceivedCount() { return 0; }

  virtual bool isMqttConnected() {
    return false;  // no op by default
  };
  virtual uint32_t getMqttOkCount() { return 0; }
  virtual uint32_t getMqttFailCount() { return 0; }
  virtual uint32_t getBridgePacketCount() { return 0; }
  virtual int getConnectedClientCount() { return -1; }  // -1 = not applicable for this role

  virtual void restartBridge() {
    // no op by default
  };

  virtual bool setRxBoostedGain(bool enable) {
    return false; // CommonCLI reports unsupported if not overridden by wrapper
  };

  #if defined(USE_LR2021)
  virtual bool configSideDetectors(const uint8_t sideDetSFs[], uint8_t num, float bw) {
    return false; // Override in wrapper
  } 
  #endif
};

class CommonCLI {
  mesh::RTCClock* _rtc;
  NodePrefs* _prefs;
  CommonCLICallbacks* _callbacks;
  mesh::MainBoard* _board;
  SensorManager* _sensors;
  RegionMap* _region_map;
  ClientACL* _acl;
  char tmp[PRV_KEY_SIZE*2 + 4];

  mesh::RTCClock* getRTCClock() { return _rtc; }
  void savePrefs();
  void loadPrefsInt(FILESYSTEM* _fs, const char* filename);

  void handleRegionCmd(char* command, char* reply);
  void handleGetCmd(uint32_t sender_timestamp, char* command, char* reply);
  void handleSetCmd(uint32_t sender_timestamp, char* command, char* reply);

public:
  CommonCLI(mesh::MainBoard& board, mesh::RTCClock& rtc, SensorManager& sensors, RegionMap& region_map, ClientACL& acl, NodePrefs* prefs, CommonCLICallbacks* callbacks)
      : _board(&board), _rtc(&rtc), _sensors(&sensors), _region_map(&region_map), _acl(&acl), _prefs(prefs), _callbacks(callbacks) { }

  void loadPrefs(FILESYSTEM* _fs);
  bool savePrefs(FILESYSTEM* _fs);
  void handleCommand(uint32_t sender_timestamp, char* command, char* reply);
  uint8_t buildAdvertData(uint8_t node_type, uint8_t* app_data);
};
