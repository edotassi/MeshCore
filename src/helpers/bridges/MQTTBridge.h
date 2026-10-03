#pragma once

#include "helpers/bridges/BridgeBase.h"

#ifdef WITH_MQTT_BRIDGE

#include <WiFi.h>
#include <PubSubClient.h>

/**
 * @brief One-way telemetry bridge that publishes mesh activity to a remote MQTT broker.
 *
 * Unlike RS232Bridge/ESPNowBridge (which relay packets between two mesh networks),
 * this bridge is an "Observer" uplink: it never re-injects data back into the mesh
 * (onPacketReceived() is a no-op), it only reports what this node sees.
 *
 * Publishes, depending on prefs:
 *  - a periodic JSON status/heartbeat message (mqtt.status)
 *  - one JSON message per logged packet (mqtt.tx / mqtt.raw, gated also by bridge.source)
 *
 * Topics follow the "meshcore/{IATA}/{node_pubkey_hex}/{status|packets}" convention
 * used by the wider MeshCore Observer/Analyzer ecosystem, so an existing community
 * dashboard (e.g. a self-hosted CoreScope instance) can ingest this node unmodified.
 */
class MQTTBridge : public BridgeBase {
  WiFiClient _wifi_client;
  PubSubClient _mqtt_client;

  uint8_t _self_pub_key[32];
  bool _have_identity = false;

  mesh::MainBoard* _board = nullptr;
  mesh::Radio* _radio = nullptr;

  unsigned long _last_status_publish = 0;
  unsigned long _last_reconnect_attempt = 0;
  bool _ntp_started = false;

  uint32_t _publish_ok_count = 0;
  uint32_t _publish_fail_count = 0;
  uint32_t _reconnect_count = 0;
  uint32_t _packets_observed_count = 0;

  float _last_rssi = 0;
  uint32_t _tx_air_secs = 0;
  uint32_t _rx_air_secs = 0;

  void ensureWifi();
  bool ensureMqtt();
  void publishStatus();
  void publishPacket(mesh::Packet* packet, const char* topic_suffix);
  void buildTopic(char* dest, size_t dest_len, const char* suffix) const;
  void trackPublish(bool ok);
  void getTimeParts(char* iso_ts, size_t iso_len, char* date_str, size_t date_len, char* time_str, size_t time_len) const;

public:
  MQTTBridge(NodePrefs* prefs, mesh::PacketManager* mgr, mesh::RTCClock* rtc);

  /**
   * @brief Gives the bridge the node's own public key, used to namespace MQTT topics.
   *        Call once, after the node identity has been loaded (before begin()/loop()).
   */
  void setIdentity(const uint8_t* pub_key, size_t len);

  /** @brief Optional: last RSSI reading, included in the next published packet JSON. */
  void setLastRssi(float rssi) { _last_rssi = rssi; }

  /** @brief Optional: board reference, used to report real battery_mv in the status heartbeat. */
  void setBoard(mesh::MainBoard* board) { _board = board; }

  /** @brief Optional: radio reference, used to report real noise_floor in the status heartbeat. */
  void setRadio(mesh::Radio* radio) { _radio = radio; }

  /** @brief Optional: cumulative TX/RX radio airtime (seconds), included in the next status heartbeat. */
  void setAirtimeStats(uint32_t tx_air_secs, uint32_t rx_air_secs) {
    _tx_air_secs = tx_air_secs;
    _rx_air_secs = rx_air_secs;
  }

  void begin() override;
  void end() override;
  void loop() override;

  /** @brief True once WiFi is up AND the MQTT client has an active session with the broker. */
  bool isMqttConnected() { return _mqtt_client.connected(); }

  uint32_t getPublishOkCount() const { return _publish_ok_count; }
  uint32_t getPublishFailCount() const { return _publish_fail_count; }
  uint32_t getReconnectCount() const { return _reconnect_count; }
  uint32_t getPacketsObservedCount() const { return _packets_observed_count; }

  void onPacketReceived(mesh::Packet* packet) override;  // no-op: uplink only
  void sendPacket(mesh::Packet* packet) override;         // publishes to MQTT
};

#endif
