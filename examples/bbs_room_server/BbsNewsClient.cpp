#include "BbsNewsClient.h"

#ifdef WITH_BBS_NEWS

#include <Arduino.h>

#define NEWS_RECONNECT_INTERVAL_MS 15000  // attesa minima tra due tentativi di connessione
#define NEWS_RECONNECT_MAX_MS      300000 // attesa massima dopo tentativi falliti di fila
#define NEWS_CONFIG_CHECK_MS       1000   // ogni quanto si rilegge la configurazione
#define NEWS_BUFFER_SIZE           512    // il payload massimo e' ~270 byte, piu' il topic

BbsNewsClient* BbsNewsClient::_instance = nullptr;

BbsNewsClient::BbsNewsClient(NodePrefs* prefs)
    : _mqtt_client(_wifi_client), _prefs(prefs), _begun(false), _cfg_generation(0), _have_cfg(false),
      _reported_connected(false), _attempt_now(true), _last_attempt(0), _retry_interval(NEWS_RECONNECT_INTERVAL_MS), _last_cfg_check(0) {
  memset(&_cfg, 0, sizeof(_cfg));
  _client_id[0] = 0;
  _instance = this;
  _mqtt_client.setBufferSize(NEWS_BUFFER_SIZE);  // il default (256 byte) e' troppo piccolo
  _mqtt_client.setSocketTimeout(5);  // connect() e' bloccante: non fermare il loop a lungo
  _mqtt_client.setCallback(mqttMessageCallback);
}

void BbsNewsClient::begin(const uint8_t* pub_key) {
  // Id univoco per nodo: il broker lega la sessione persistente a questo id.
  snprintf(_client_id, sizeof(_client_id), "bbs-news-");
  size_t n = strlen(_client_id);
  for (int i = 0; i < 8; i++) snprintf(_client_id + n + i * 2, 3, "%02x", pub_key[i]);
  _begun = true;
}

void BbsNewsClient::mqttMessageCallback(char* topic, uint8_t* payload, unsigned int length) {
  (void)topic;  // il filtro e' gia' il topic sottoscritto; la fiducia viene dalla firma HMAC
  if (_instance == nullptr) return;
  const char* name = "?";
  int r = bbs::port::newsIngest(payload, length, &name);
  if (r < 0) return;
  Serial.printf("BBS news: %s\n", name);
}

void BbsNewsClient::ensureWifi() {
  // Stessa logica di MQTTWormholeBridge::ensureWifi; niente WiFi.disconnect,
  // la connessione e' condivisa con gli altri bridge.
  if (WiFi.status() == WL_CONNECTED) return;
  if (WiFi.getMode() != WIFI_STA || _prefs->wifi_ssid[0] == 0) {
    if (_prefs->wifi_ssid[0] == 0) return;  // non ancora configurato
    WiFi.mode(WIFI_STA);
    WiFi.begin(_prefs->wifi_ssid, _prefs->wifi_pwd);
  }
}

void BbsNewsClient::reportConnected(bool connected) {
  if (connected == _reported_connected) return;
  _reported_connected = connected;
  bbs::port::newsSetConnected(connected);
}

void BbsNewsClient::connectMqtt() {
  _mqtt_client.setServer(_cfg.server, _cfg.port);
  bool ok;
  if (_cfg.username[0] != 0) {
    ok = _mqtt_client.connect(_client_id, _cfg.username, _cfg.password, nullptr, 0, false, nullptr, false);
  } else {
    ok = _mqtt_client.connect(_client_id, nullptr, nullptr, nullptr, 0, false, nullptr, false);
  }
  if (!ok) {
    Serial.printf("BBS news: connessione MQTT fallita (stato %d)\n", _mqtt_client.state());
    return;
  }
  if (!_mqtt_client.subscribe(_cfg.topic, 1)) {
    Serial.println("BBS news: iscrizione al topic fallita");
    _mqtt_client.disconnect();
    return;
  }
  Serial.println("BBS news: connesso e iscritto");
}

void BbsNewsClient::loop() {
  if (!_begun) return;

  unsigned long now = millis();

  // Configurazione: si rilegge una volta al secondo; se e' cambiata (comando
  // NEWS) si chiude la connessione e si riparte con i nuovi valori.
  if (!_have_cfg || now - _last_cfg_check >= NEWS_CONFIG_CHECK_MS) {
    _last_cfg_check = now;
    bbs::port::NewsClientConfig cfg;
    uint32_t gen;
    if (!bbs::port::newsGetConfig(cfg, gen)) return;
    if (!_have_cfg || gen != _cfg_generation) {
      if (_mqtt_client.connected()) _mqtt_client.disconnect();
      reportConnected(false);
      _cfg = cfg;
      _cfg_generation = gen;
      _have_cfg = true;
      _attempt_now = true;
      _retry_interval = NEWS_RECONNECT_INTERVAL_MS;
    } else {
      _cfg.enabled = cfg.enabled;  // puo' cambiare senza nuova generazione (orologio impostato)
    }
  }
  if (!_have_cfg) return;

  if (!_cfg.enabled || _cfg.server[0] == 0) {
    if (_mqtt_client.connected()) _mqtt_client.disconnect();
    reportConnected(false);
    return;
  }

  ensureWifi();
  if (WiFi.status() != WL_CONNECTED) {
    reportConnected(false);
    return;
  }

  if (_mqtt_client.connected()) {
    _mqtt_client.loop();
    reportConnected(_mqtt_client.connected());
    return;
  }
  reportConnected(false);

  // connect() blocca il loop (e quindi la radio) per qualche secondo se il
  // broker non risponde: dopo ogni fallimento l'attesa raddoppia, fino a 5 min.
  if (!_attempt_now && now - _last_attempt < _retry_interval) return;
  _attempt_now = false;
  _last_attempt = now;
  connectMqtt();
  if (_mqtt_client.connected()) {
    _retry_interval = NEWS_RECONNECT_INTERVAL_MS;
  } else if (_retry_interval < NEWS_RECONNECT_MAX_MS) {
    _retry_interval = _retry_interval * 2 > NEWS_RECONNECT_MAX_MS ? NEWS_RECONNECT_MAX_MS : _retry_interval * 2;
  }
  reportConnected(_mqtt_client.connected());
}

#endif // WITH_BBS_NEWS
