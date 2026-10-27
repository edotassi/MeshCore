#pragma once

#ifdef WITH_BBS_NEWS

#include <WiFi.h>
#include <PubSubClient.h>

#include <helpers/CommonCLI.h>

#include "bbs_port_adapter.h"

/**
 * Client MQTT delle news della BBS (vedi lib/bbs/bbs_news.h per il formato
 * dei messaggi). Si iscrive al topic configurato con il comando NEWS e passa
 * ogni payload a bbs::port::newsIngest(), che verifica la firma e accoda il
 * titolo nella stanza delle news. Indipendente da MQTTBridge e
 * MQTTWormholeBridge: ha un suo WiFiClient e un suo PubSubClient.
 *
 * Sessione persistente (cleanSession = false) e iscrizione QoS 1: il broker
 * accoda le news mentre il nodo e' spento e le consegna alla riconnessione.
 * Il WiFi usa gli stessi parametri dei bridge (set wifi.ssid / wifi.pwd da
 * seriale).
 */
class BbsNewsClient {
  WiFiClient _wifi_client;
  PubSubClient _mqtt_client;
  NodePrefs* _prefs;

  char _client_id[40];
  bool _begun;
  bbs::port::NewsClientConfig _cfg;
  uint32_t _cfg_generation;
  bool _have_cfg;
  bool _reported_connected;
  bool _attempt_now;  // salta l'attesa tra due tentativi (config appena cambiata)
  unsigned long _last_attempt;
  unsigned long _retry_interval;  // attesa prima del prossimo tentativo (cresce dopo ogni fallimento)
  unsigned long _last_cfg_check;

  static BbsNewsClient* _instance;
  static void mqttMessageCallback(char* topic, uint8_t* payload, unsigned int length);

  void ensureWifi();
  void reportConnected(bool connected);
  void connectMqtt();

public:
  explicit BbsNewsClient(NodePrefs* prefs);

  // Da chiamare in MyMesh::begin() dopo bbs::port::init(): 'pub_key' (32
  // byte) ricava l'id client MQTT.
  void begin(const uint8_t* pub_key);
  void loop();
};

#endif // WITH_BBS_NEWS
