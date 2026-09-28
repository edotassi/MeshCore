#include "helpers/bridges/MQTTBridge.h"

#ifdef WITH_MQTT_BRIDGE

#include <MeshCore.h>
#include <time.h>

#define MQTT_RECONNECT_INTERVAL  10000  // ms between reconnect attempts
#define MQTT_MIN_INTERVAL        5000   // floor for mqtt.interval, ms

#ifndef MQTT_BRIDGE_MODEL
#define MQTT_BRIDGE_MODEL "Heltec V4 OLED"
#endif
#ifndef MQTT_BRIDGE_FW_VERSION
#define MQTT_BRIDGE_FW_VERSION "1.0.0-mqtt-custom"
#endif

MQTTBridge::MQTTBridge(NodePrefs* prefs, mesh::PacketManager* mgr, mesh::RTCClock* rtc)
    : BridgeBase(prefs, mgr, rtc), _mqtt_client(_wifi_client) {
  memset(_self_pub_key, 0, sizeof(_self_pub_key));
  _mqtt_client.setBufferSize(1200);  // default 256B is too small; packet payloads include a full hex dump
}

void MQTTBridge::setIdentity(const uint8_t* pub_key, size_t len) {
  memset(_self_pub_key, 0, sizeof(_self_pub_key));
  memcpy(_self_pub_key, pub_key, len < sizeof(_self_pub_key) ? len : sizeof(_self_pub_key));
  _have_identity = true;
}

void MQTTBridge::buildTopic(char* dest, size_t dest_len, const char* suffix) const {
  char pub_hex[65];
  mesh::Utils::toHex(pub_hex, _self_pub_key, sizeof(_self_pub_key));

  const char* iata = (_prefs->mqtt_iata[0] != 0) ? _prefs->mqtt_iata : "XXX";
  snprintf(dest, dest_len, "meshcore/%s/%s/%s", iata, pub_hex, suffix);
}

void MQTTBridge::ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!_ntp_started) {
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");  // UTC, no DST offset
      _ntp_started = true;
    }
    return;
  }
  if (WiFi.getMode() != WIFI_STA || _prefs->wifi_ssid[0] == 0) {
    if (_prefs->wifi_ssid[0] == 0) return;  // not configured yet
    WiFi.mode(WIFI_STA);
    WiFi.begin(_prefs->wifi_ssid, _prefs->wifi_pwd);
  }
}

void MQTTBridge::getTimeParts(char* iso_ts, size_t iso_len, char* date_str, size_t date_len, char* time_str, size_t time_len) const {
  time_t now;
  time(&now);
  struct tm tm_info;
  if (now < 8 * 3600 * 2) {  // not synced yet (epoch ~ 1970)
    memset(&tm_info, 0, sizeof(tm_info));
    tm_info.tm_year = 70; tm_info.tm_mon = 0; tm_info.tm_mday = 1;
  } else {
    gmtime_r(&now, &tm_info);
  }
  size_t n = strftime(iso_ts, iso_len, "%Y-%m-%dT%H:%M:%S", &tm_info);
  snprintf(iso_ts + n, iso_len - n, ".%06lu", (unsigned long)(millis() % 1000) * 1000UL);
  strftime(date_str, date_len, "%d/%m/%Y", &tm_info);
  strftime(time_str, time_len, "%H:%M:%S", &tm_info);
}

bool MQTTBridge::ensureMqtt() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (_mqtt_client.connected()) return true;

  unsigned long now = millis();
  if (now - _last_reconnect_attempt < MQTT_RECONNECT_INTERVAL) return false;
  _last_reconnect_attempt = now;

  if (_prefs->mqtt_server[0] == 0) return false;  // not configured

  _mqtt_client.setServer(_prefs->mqtt_server, _prefs->mqtt_port);

  char client_id[24];
  mesh::Utils::toHex(client_id, _self_pub_key, 8);  // short id, first 8 bytes is enough for uniqueness

  bool ok;
  if (_prefs->mqtt_username[0] != 0) {
    ok = _mqtt_client.connect(client_id, _prefs->mqtt_username, _prefs->mqtt_password);
  } else {
    ok = _mqtt_client.connect(client_id);
  }
  if (ok) _reconnect_count++;
  return ok;
}

void MQTTBridge::trackPublish(bool ok) {
  if (ok) {
    _publish_ok_count++;
  } else {
    _publish_fail_count++;
  }
}

void MQTTBridge::publishStatus() {
  char topic[96];
  buildTopic(topic, sizeof(topic), "status");

  char pub_hex[65];
  mesh::Utils::toHex(pub_hex, _self_pub_key, sizeof(_self_pub_key));

  char timestamp[32], date_str[16], time_str[16];
  getTimeParts(timestamp, sizeof(timestamp), date_str, sizeof(date_str), time_str, sizeof(time_str));

  char radio[32];
  snprintf(radio, sizeof(radio), "%.6f,%.1f,%d,%d", _prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr);

  char payload[384];
  snprintf(payload, sizeof(payload),
           "{\"status\":\"online\",\"timestamp\":\"%s\",\"origin\":\"%s\",\"origin_id\":\"%s\","
           "\"radio\":\"%s\",\"model\":\"" MQTT_BRIDGE_MODEL "\",\"firmware_version\":\"" MQTT_BRIDGE_FW_VERSION "\","
           "\"client_version\":\"meshcore-mqtt-bridge/" MQTT_BRIDGE_FW_VERSION "\","
           "\"stats\":{\"battery_mv\":0,\"uptime_secs\":%lu,\"errors\":%u,\"queue_len\":0,"
           "\"noise_floor\":0,\"tx_air_secs\":0,\"rx_air_secs\":0}}",
           timestamp, _prefs->mqtt_origin, pub_hex, radio,
           millis() / 1000, (unsigned)_publish_fail_count);

  trackPublish(_mqtt_client.publish(topic, payload, true));  // retained
}

void MQTTBridge::publishPacket(mesh::Packet* packet, const char* topic_suffix) {
  char topic[96];
  buildTopic(topic, sizeof(topic), topic_suffix);

  char pub_hex[65];
  mesh::Utils::toHex(pub_hex, _self_pub_key, sizeof(_self_pub_key));

  char timestamp[32], date_str[16], time_str[16];
  getTimeParts(timestamp, sizeof(timestamp), date_str, sizeof(date_str), time_str, sizeof(time_str));

  uint8_t raw_bytes[280];
  uint8_t raw_len = packet->writeTo(raw_bytes);
  char raw_hex[561];
  mesh::Utils::toHex(raw_hex, raw_bytes, raw_len);

  uint8_t hash[MAX_HASH_SIZE];
  packet->calculatePacketHash(hash);
  char hash_hex[MAX_HASH_SIZE * 2 + 1];
  mesh::Utils::toHex(hash_hex, hash, MAX_HASH_SIZE);

  const char* direction = _prefs->bridge_pkt_src ? "rx" : "tx";

  char payload[1024];
  snprintf(payload, sizeof(payload),
           "{\"origin\":\"%s\",\"origin_id\":\"%s\",\"timestamp\":\"%s\",\"type\":\"PACKET\","
           "\"direction\":\"%s\",\"time\":\"%s\",\"date\":\"%s\",\"len\":\"%d\",\"packet_type\":\"%d\","
           "\"route\":\"%s\",\"payload_len\":\"%d\",\"raw\":\"%s\",\"SNR\":\"%.1f\",\"RSSI\":\"%.0f\",\"hash\":\"%s\"}",
           _prefs->mqtt_origin, pub_hex, timestamp,
           direction, time_str, date_str, (int)raw_len, (int)packet->getPayloadType(),
           packet->isRouteDirect() ? "D" : "F", (int)packet->payload_len, raw_hex,
           packet->getSNR(), _last_rssi, hash_hex);

  trackPublish(_mqtt_client.publish(topic, payload, false));
}

void MQTTBridge::begin() {
  _initialized = true;
  _last_status_publish = 0;
  _last_reconnect_attempt = 0;
}

void MQTTBridge::end() {
  _mqtt_client.disconnect();
  WiFi.mode(WIFI_OFF);
  _initialized = false;
}

void MQTTBridge::loop() {
  if (!_initialized || !_have_identity) return;

  ensureWifi();
  if (!ensureMqtt()) return;

  _mqtt_client.loop();

  if (_prefs->mqtt_status_enabled) {
    uint32_t interval = _prefs->mqtt_interval >= MQTT_MIN_INTERVAL ? _prefs->mqtt_interval : MQTT_MIN_INTERVAL;
    unsigned long now = millis();
    if (now - _last_status_publish >= interval) {
      publishStatus();
      _last_status_publish = now;
    }
  }
}

void MQTTBridge::onPacketReceived(mesh::Packet* packet) {
  // Observer uplink is one-way: never called (nothing external gets injected into the mesh),
  // kept as a harmless no-op only to satisfy the AbstractBridge interface.
}

void MQTTBridge::sendPacket(mesh::Packet* packet) {
  // Read-only peek, same contract as ESPNowBridge/RS232Bridge::sendPacket(): the packet
  // is still owned by the mesh core's packet manager, which frees/reuses it on its own -
  // this bridge must NOT free it.
  _packets_observed_count++;
  if (_have_identity && _mqtt_client.connected()) {
    if (_prefs->mqtt_raw_enabled) {
      publishPacket(packet, "raw");
    }
    if (_prefs->mqtt_tx_enabled) {
      publishPacket(packet, "packets");
    }
  }
}

#endif
