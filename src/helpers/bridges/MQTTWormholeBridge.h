#pragma once

#include "helpers/bridges/BridgeBase.h"

#ifdef WITH_MQTT_WORMHOLE_BRIDGE

#include <WiFi.h>
#include <PubSubClient.h>

/**
 * @brief Point-to-point relay bridge between exactly two paired instances of this firmware,
 *        connected over a private MQTT broker (independent from MQTTBridge's public observer link).
 *
 * Every packet received over LoRa (logRx()) is published as-is (JSON + hex, same format as
 * MQTTBridge::publishPacket()) to `wormhole_pub_topic`. Whatever arrives on `wormhole_sub_topic`
 * is decoded and re-injected into this node's mesh, to be retransmitted over its own LoRa.
 */
class MQTTWormholeBridge : public BridgeBase {
  WiFiClient _wifi_client;
  PubSubClient _mqtt_client;
  static MQTTWormholeBridge* _instance;
  static void mqttMessageCallback(char* topic, uint8_t* payload, unsigned int length);

  unsigned long _last_reconnect_attempt = 0;
  uint32_t _sent_count = 0;
  uint32_t _received_count = 0;

  // Tracking dei pacchetti iniettati, per forzarne l'inoltro indipendentemente da disable_fwd
  static const int MAX_TRACKED_PACKETS = 4;  // ring buffer, i pacchetti restano allocati poco
  const mesh::Packet* _injected_packets[MAX_TRACKED_PACKETS] = {};
  int _injected_packets_idx = 0;
  void trackInjectedPacket(const mesh::Packet* pkt);

  void ensureWifi();
  bool ensureMqtt();
  void onMqttMessage(const char* topic, const uint8_t* payload, unsigned int length);
  bool injectPacketFromJson(const uint8_t* json, unsigned int length);

public:
  MQTTWormholeBridge(NodePrefs* prefs, mesh::PacketManager* mgr, mesh::RTCClock* rtc);
  void begin() override;
  void end() override;
  void loop() override;
  void onPacketReceived(mesh::Packet* packet) override;  // -> handleReceivedPacket()
  void sendPacket(mesh::Packet* packet) override;         // pubblica su wormhole_pub_topic
  bool isMqttConnected() { return _mqtt_client.connected(); }

  /** true se `packet` è stato iniettato da questo bridge (va forzato in allowPacketForward) */
  bool wasInjectedByWormhole(const mesh::Packet* packet) const;

  uint32_t getSentCount() const { return _sent_count; }
  uint32_t getReceivedCount() const { return _received_count; }
};

#endif
