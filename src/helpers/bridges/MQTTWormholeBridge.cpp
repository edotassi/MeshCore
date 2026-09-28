#include "helpers/bridges/MQTTWormholeBridge.h"

#ifdef WITH_MQTT_WORMHOLE_BRIDGE

#include <MeshCore.h>

#define MQTT_RECONNECT_INTERVAL  10000  // ms between reconnect attempts

MQTTWormholeBridge* MQTTWormholeBridge::_instance = nullptr;

MQTTWormholeBridge::MQTTWormholeBridge(NodePrefs* prefs, mesh::PacketManager* mgr, mesh::RTCClock* rtc)
    : BridgeBase(prefs, mgr, rtc), _mqtt_client(_wifi_client) {
  _instance = this;
  _mqtt_client.setBufferSize(1200);  // default 256B is too small; packet payloads include a full hex dump
}

void MQTTWormholeBridge::mqttMessageCallback(char* topic, uint8_t* payload, unsigned int length) {
  if (_instance) {
    _instance->onMqttMessage(topic, payload, length);
  }
}

void MQTTWormholeBridge::ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (WiFi.getMode() != WIFI_STA || _prefs->wifi_ssid[0] == 0) {
    if (_prefs->wifi_ssid[0] == 0) return;  // not configured yet
    WiFi.mode(WIFI_STA);
    WiFi.begin(_prefs->wifi_ssid, _prefs->wifi_pwd);
  }
}

bool MQTTWormholeBridge::ensureMqtt() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (_mqtt_client.connected()) return true;

  unsigned long now = millis();
  if (now - _last_reconnect_attempt < MQTT_RECONNECT_INTERVAL) return false;
  _last_reconnect_attempt = now;

  if (_prefs->wormhole_server[0] == 0) return false;  // not configured

  _mqtt_client.setServer(_prefs->wormhole_server, _prefs->wormhole_port);

  char client_id[48];
  snprintf(client_id, sizeof(client_id), "wormhole-%s", _prefs->wormhole_pub_topic[0] ? _prefs->wormhole_pub_topic : "node");

  bool ok;
  if (_prefs->wormhole_username[0] != 0) {
    ok = _mqtt_client.connect(client_id, _prefs->wormhole_username, _prefs->wormhole_password);
  } else {
    ok = _mqtt_client.connect(client_id);
  }
  if (ok && _prefs->wormhole_sub_topic[0] != 0) {
    _mqtt_client.subscribe(_prefs->wormhole_sub_topic);  // subscriptions must be redone on every reconnect
  }
  return ok;
}

void MQTTWormholeBridge::begin() {
  _mqtt_client.setCallback(mqttMessageCallback);
  _initialized = true;
  _last_reconnect_attempt = 0;
}

void MQTTWormholeBridge::end() {
  _mqtt_client.disconnect();
  _initialized = false;
  // NOTE: WiFi is left running - it may still be in use by other bridges/features (e.g. MQTTBridge).
}

void MQTTWormholeBridge::loop() {
  if (!_initialized) return;

  ensureWifi();
  if (!ensureMqtt()) return;

  _mqtt_client.loop();
}

void MQTTWormholeBridge::trackInjectedPacket(const mesh::Packet* pkt) {
  _injected_packets[_injected_packets_idx] = pkt;
  _injected_packets_idx = (_injected_packets_idx + 1) % MAX_TRACKED_PACKETS;
}

bool MQTTWormholeBridge::wasInjectedByWormhole(const mesh::Packet* packet) const {
  for (int i = 0; i < MAX_TRACKED_PACKETS; i++) {
    if (_injected_packets[i] == packet) return true;
  }
  return false;
}

bool MQTTWormholeBridge::injectPacketFromJson(const uint8_t* json, unsigned int length) {
  static const char MARKER[] = "\"raw\":\"";
  const unsigned int marker_len = sizeof(MARKER) - 1;

  int hex_start = -1;
  for (unsigned int i = 0; i + marker_len <= length; i++) {
    if (memcmp(json + i, MARKER, marker_len) == 0) {
      hex_start = i + marker_len;
      break;
    }
  }
  if (hex_start < 0) return false;

  char hex[561];  // same size as MQTTBridge's raw_hex: enough for a 280-byte packet
  unsigned int hex_len = 0;
  for (unsigned int i = (unsigned int)hex_start; i < length && hex_len < sizeof(hex) - 1; i++) {
    if (json[i] == '"') break;
    hex[hex_len++] = (char)json[i];
  }
  hex[hex_len] = 0;

  if (hex_len == 0 || (hex_len % 2) != 0) return false;

  int decoded_len = hex_len / 2;
  uint8_t decoded[280];
  if (decoded_len > (int)sizeof(decoded)) return false;
  if (!mesh::Utils::fromHex(decoded, decoded_len, hex)) return false;

  mesh::Packet* pkt = _mgr->allocNew();
  if (!pkt) return false;

  if (pkt->readFrom(decoded, decoded_len)) {
    trackInjectedPacket(pkt);  // marca il pacchetto per il bypass di disable_fwd
    onPacketReceived(pkt);     // -> handleReceivedPacket() -> _mgr->queueInbound(...)
    _received_count++;
    return true;
  }
  _mgr->free(pkt);
  return false;
}

void MQTTWormholeBridge::onMqttMessage(const char* topic, const uint8_t* payload, unsigned int length) {
  injectPacketFromJson(payload, length);
}

void MQTTWormholeBridge::onPacketReceived(mesh::Packet* packet) {
  handleReceivedPacket(packet);
}

void MQTTWormholeBridge::sendPacket(mesh::Packet* packet) {
  // Read-only peek, same contract as MQTTBridge::sendPacket(): the packet is still owned
  // by the mesh core's packet manager, which frees/reuses it on its own - must NOT free it.
  if (!_mqtt_client.connected()) return;
  if (_prefs->wormhole_pub_topic[0] == 0) return;

  uint8_t raw_bytes[280];
  uint8_t raw_len = packet->writeTo(raw_bytes);
  char raw_hex[561];
  mesh::Utils::toHex(raw_hex, raw_bytes, raw_len);

  uint8_t hash[MAX_HASH_SIZE];
  packet->calculatePacketHash(hash);
  char hash_hex[MAX_HASH_SIZE * 2 + 1];
  mesh::Utils::toHex(hash_hex, hash, MAX_HASH_SIZE);

  char payload[1024];
  snprintf(payload, sizeof(payload),
           "{\"timestamp\":\"%s\",\"type\":\"PACKET\",\"direction\":\"rx\",\"len\":\"%d\","
           "\"packet_type\":\"%d\",\"route\":\"%s\",\"payload_len\":\"%d\",\"raw\":\"%s\","
           "\"SNR\":\"%.1f\",\"hash\":\"%s\"}",
           getLogDateTime(), (int)raw_len, (int)packet->getPayloadType(),
           packet->isRouteDirect() ? "D" : "F", (int)packet->payload_len, raw_hex,
           packet->getSNR(), hash_hex);

  if (_mqtt_client.publish(_prefs->wormhole_pub_topic, payload, false)) {
    _sent_count++;
  }
}

#endif
