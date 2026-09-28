# MQTT Wormhole Bridge — Piano di implementazione

> Stato: design, non ancora implementato.

## Obiettivo

Ogni volta che il modulo LoRa riceve correttamente un pacchetto, inoltrarlo via internet a
un'unica istanza remota "gemella" dello stesso firmware, che lo ritrasmette sulla propria
LoRa. Collegamento privato, punto-punto, tra esattamente due istanze specifiche (non un
bridge broadcast/multi-nodo come `ESPNowBridge`).

## Contesto

- Collega due punti a ~1200 km di distanza (Nord Italia e Sud Italia).
- Topologia fissa e permanente a 2 nodi: nessun incremento futuro previsto.
- Entrambi i dispositivi in Italia (stessa banda LoRa / stessa regolamentazione EU868).

## Architettura

- [`AbstractBridge`](../src/helpers/AbstractBridge.h): contratto `sendPacket(packet)` /
  `onPacketReceived(packet)` implementato da ogni bridge.
- [`BridgeBase`](../src/helpers/bridges/BridgeBase.h): logica condivisa (checksum
  Fletcher-16, `SimpleMeshTables`, timestamp).
- [`MQTTBridge`](../src/helpers/bridges/MQTTBridge.h) (esistente, non modificato): uplink
  observer one-way verso un broker MQTT pubblico, formato JSON + hex.
- `MQTTWormholeBridge` (nuova classe, vedi Piano di implementazione): connessione MQTT
  indipendente da `MQTTBridge`, verso il broker privato Coolify. Le due classi girano in
  parallelo, connesse a broker diversi con credenziali diverse.

## Decisioni di design

| Argomento | Decisione |
|---|---|
| Topologia | Punto-punto, esattamente 2 istanze accoppiate |
| Trasporto | MQTT, tramite nuova classe `MQTTWormholeBridge` con connessione WiFi/MQTT propria |
| Broker | Mosquitto self-hosted su Coolify — `167.233.95.98:1883`, utente `meshcore` |
| Relazione con l'observer MQTT | Connessioni indipendenti e simultanee: `MQTTBridge` (broker pubblico, invariato) e `MQTTWormholeBridge` (broker privato) |
| Instradamento | Coppia di topic dedicata al collegamento (pub/sub incrociati tra i due nodi), non la convenzione observer `meshcore/{IATA}/{pubkey}/...` |
| Formato payload | JSON + hex, stesso formato di `MQTTBridge::publishPacket()` |
| Anti-loop / deduplica | Non implementata |
| Cifratura applicativa | Nessuna; autenticazione solo a livello broker (username/password Mosquitto) |
| Trigger | Solo su ricezione LoRa (`logRx()`), mai su trasmissione |
| Accensione/spegnimento | `set wormhole.en on\|off`, toggle live senza riavvio |
| Configurazione | CLI `wormhole.*`, namespace indipendente da `mqtt.*` |
| Forwarding sul nodo ricevente | `allowPacketForward()` bypassa solo il controllo `disable_fwd` per i pacchetti iniettati dal wormhole (`wormhole.wasInjectedByWormhole()`); gli altri controlli (limite hop flood) restano invariati. Un room server resta room server (non ritrasmette il traffico locale) e fa da relay solo per il wormhole; su un repeater (default `disable_fwd=0`) il comportamento per tutto il resto del traffico non cambia |

## Piano di implementazione

Checklist file per file, con riferimenti al codice esistente.

### 1. `src/helpers/CommonCLI.h` — nuovi campi prefs

Nuovo blocco indipendente dai campi `mqtt_*` esistenti:

```cpp
// Wormhole bridge settings (independent MQTT connection, separate broker from mqtt_* above)
uint8_t wormhole_enabled = 0;
char wormhole_server[64];
uint16_t wormhole_port = 1883;
char wormhole_username[32];
char wormhole_password[32];
char wormhole_pub_topic[64];
char wormhole_sub_topic[64];
```

- [ ] Aggiungere i 7 campi in `NodePrefs`.
- [ ] Aggiungere una nested class `WormholePrefs` (stesso pattern di `MqttPrefs`, righe
      ~182-202):
      ```cpp
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
      ```
- [ ] Registrare `def("wormhole", wormhole);` nel `structure()` principale di `NodePrefs`
      (riga ~220, accanto a `def("mqtt", mqtt);`).
- [ ] Aggiungere `wormhole(this)` alla lista di inizializzazione del costruttore (riga ~224,
      accanto a `mqtt(this)`).
- [ ] Inizializzare le stringhe nel corpo del costruttore:
      ```cpp
      wormhole_server[0] = 0;
      wormhole_username[0] = 0;
      wormhole_password[0] = 0;
      wormhole_pub_topic[0] = 0;
      wormhole_sub_topic[0] = 0;
      ```

### 2. `src/helpers/CommonCLI.cpp` — comandi CLI `set`/`get` sotto `wormhole.*`

Nuovo blocco `#ifdef WITH_MQTT_WORMHOLE_BRIDGE` nei comandi `set` (accanto, non dentro, al
blocco `#ifdef WITH_MQTT_BRIDGE` esistente), stesso pattern di `mqtt.server`/`mqtt.status`:

- [ ] `set wormhole.en on|off` — toggle live, non solo un flag persistito:
      ```cpp
      } else if (memcmp(config, "wormhole.en ", 12) == 0) {
        _prefs->wormhole_enabled = memcmp(&config[12], "on", 2) == 0;
        savePrefs();
        _callbacks->setWormholeState(_prefs->wormhole_enabled);   // vedi punto 5
        strcpy(reply, "OK");
      ```
- [ ] `set wormhole.server <host>`
- [ ] `set wormhole.port <port>` (validazione 1-65535, come `mqtt.port`)
- [ ] `set wormhole.user <user>`
- [ ] `set wormhole.pass <pass>`
- [ ] `set wormhole.pub <topic>`
- [ ] `set wormhole.sub <topic>`

Stesso set, speculare, nel blocco `get` per ciascun campo (password mascherata come
`********` se non vuota, come `mqtt.password`).

### 3. `src/helpers/bridges/MQTTWormholeBridge.h` — nuova classe (file nuovo)

```cpp
#pragma once
#include "helpers/bridges/BridgeBase.h"
#ifdef WITH_MQTT_WORMHOLE_BRIDGE
#include <WiFi.h>
#include <PubSubClient.h>

class MQTTWormholeBridge : public BridgeBase {
  WiFiClient _wifi_client;
  PubSubClient _mqtt_client;
  static MQTTWormholeBridge* _instance;
  static void mqttMessageCallback(char* topic, uint8_t* payload, unsigned int length);

  unsigned long _last_reconnect_attempt = 0;

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
};
#endif
```

`ensureWifi()`/`ensureMqtt()` rispecchiano gli omonimi metodi di
[`MQTTBridge`](../src/helpers/bridges/MQTTBridge.h), leggendo `_prefs->wormhole_*` invece di
`_prefs->mqtt_*`.

### 4. `src/helpers/bridges/MQTTWormholeBridge.cpp` — implementazione (file nuovo)

- [ ] `ensureWifi()`: identico a
      [MQTTBridge.cpp:38-51](../src/helpers/bridges/MQTTBridge.cpp#L38-L51).
- [ ] `ensureMqtt()`: come [MQTTBridge.cpp:69-92](../src/helpers/bridges/MQTTBridge.cpp#L69-L92)
      ma con `_prefs->wormhole_server`/`wormhole_port`/`wormhole_username`/`wormhole_password`.
      Dopo una connessione riuscita, se `wormhole_sub_topic[0] != 0`:
      `_mqtt_client.subscribe(_prefs->wormhole_sub_topic);` (le subscription vanno rifatte a
      ogni riconnessione).
- [ ] Costruttore: `_instance = this;` (pattern `ESPNowBridge::_instance`,
      [ESPNowBridge.cpp:26](../src/helpers/bridges/ESPNowBridge.cpp#L26)).
- [ ] `begin()`: `_mqtt_client.setCallback(mqttMessageCallback);`, poi come
      [MQTTBridge.cpp:163-167](../src/helpers/bridges/MQTTBridge.cpp#L163-L167).
- [ ] `mqttMessageCallback()`: wrapper statico → `_instance->onMqttMessage(topic, payload,
      length)` (pattern `ESPNowBridge::recv_cb`,
      [ESPNowBridge.cpp:12-16](../src/helpers/bridges/ESPNowBridge.cpp#L12-L16)).
- [ ] `onMqttMessage()` → chiama `injectPacketFromJson(payload, length)`.
- [ ] `injectPacketFromJson()` — senza dipendenza JSON (il progetto non usa ArduinoJson):
      cercare la sottostringa `"raw":"`, leggere gli hex fino alla `"` di chiusura in un
      buffer locale (`char hex[561]`, stessa dimensione di `raw_hex` in
      `MQTTBridge::publishPacket()`), poi:
      ```cpp
      uint8_t decoded[280];
      if (!mesh::Utils::fromHex(decoded, sizeof(decoded), hex)) return false;

      mesh::Packet* pkt = _mgr->allocNew();
      if (!pkt) return false;

      if (pkt->readFrom(decoded, strlen(hex) / 2)) {
        trackInjectedPacket(pkt);  // marca il pacchetto per il bypass di disable_fwd
        onPacketReceived(pkt);     // -> handleReceivedPacket() -> _mgr->queueInbound(...)
        return true;
      }
      _mgr->free(pkt);
      return false;
      ```
      (`mesh::Utils::fromHex` dichiarato in [Utils.h:67](../src/Utils.h#L67)).
- [ ] `onPacketReceived()`:
      ```cpp
      void MQTTWormholeBridge::onPacketReceived(mesh::Packet* packet) {
        handleReceivedPacket(packet);   // da BridgeBase, come ESPNowBridge::onPacketReceived
      }
      ```
- [ ] `trackInjectedPacket()` / `wasInjectedByWormhole()` — ring buffer di puntatori, per
      permettere a `MyMesh::allowPacketForward()` di riconoscere i pacchetti del wormhole
      (vedi punto 5):
      ```cpp
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
      ```
- [ ] `sendPacket()`: come `MQTTBridge::publishPacket()`
      ([MQTTBridge.cpp:128-161](../src/helpers/bridges/MQTTBridge.cpp#L128-L161)), stesso
      formato JSON/hex, ma pubblica direttamente su `_prefs->wormhole_pub_topic` (topic
      assoluto, non costruito con `buildTopic()`/convenzione `meshcore/{IATA}/...`).

### 5. Wiring in `MyMesh.h`/`MyMesh.cpp`

- [ ] In `MyMesh.h`, accanto al membro `bridge` (riga ~121-122 di
      [MyMesh.h](../examples/simple_repeater/MyMesh.h)), in un blocco `#if` separato (deve
      coesistere con `WITH_MQTT_BRIDGE`):
      ```cpp
      #if defined(WITH_MQTT_WORMHOLE_BRIDGE)
        MQTTWormholeBridge wormhole;
      #endif
      ```
- [ ] Nel costruttore di `MyMesh` (accanto a `, bridge(&_prefs, _mgr, &rtc)`,
      [MyMesh.cpp:896-898](../examples/simple_repeater/MyMesh.cpp#L896-L898)):
      ```cpp
      #if defined(WITH_MQTT_WORMHOLE_BRIDGE)
            , wormhole(&_prefs, _mgr, &rtc)
      #endif
      ```
- [ ] In `MyMesh::begin()` (accanto a `if (_prefs.bridge_enabled) { bridge.begin(); }`,
      [MyMesh.cpp:1001-1007](../examples/simple_repeater/MyMesh.cpp#L1001-L1007)):
      ```cpp
      #if defined(WITH_MQTT_WORMHOLE_BRIDGE)
        if (_prefs.wormhole_enabled) wormhole.begin();
      #endif
      ```
- [ ] Toggle live di `wormhole.en` (accensione/spegnimento a caldo, senza riavvio):
      1. In [CommonCLI.h:276-278](../src/helpers/CommonCLI.h#L276-L278), accanto a
         `virtual void setBridgeState(bool enable) { };`:
         ```cpp
         virtual void setWormholeState(bool enable) {
           // no op by default
         };
         ```
      2. In `MyMesh.h`, override dello stesso pattern di `setBridgeState()`
         ([MyMesh.h:257-267](../examples/simple_repeater/MyMesh.h#L257-L267)):
         ```cpp
         #if defined(WITH_MQTT_WORMHOLE_BRIDGE)
         void setWormholeState(bool enable) override {
           if (enable == wormhole.isRunning()) return;
           if (enable) wormhole.begin();
           else wormhole.end();
         }
         #endif
         ```
- [ ] Nel loop principale (accanto a `bridge.loop();`,
      [MyMesh.cpp:1431](../examples/simple_repeater/MyMesh.cpp#L1431)):
      ```cpp
      #if defined(WITH_MQTT_WORMHOLE_BRIDGE)
        wormhole.loop();
      #endif
      ```
- [ ] In `logRx()` **soltanto** (accanto a `bridge.sendPacket(pkt);` sotto `#ifdef
      WITH_BRIDGE`, [MyMesh.cpp:490-497](../examples/simple_repeater/MyMesh.cpp#L490-L497)) —
      **non** in `logTx()`, il wormhole inoltra solo ciò che riceve via radio:
      ```cpp
      #if defined(WITH_MQTT_WORMHOLE_BRIDGE)
        if (_prefs.wormhole_enabled) wormhole.sendPacket(pkt);
      #endif
      ```
- [ ] Includere il nuovo header in `MyMesh.h`:
      ```cpp
      #ifdef WITH_MQTT_WORMHOLE_BRIDGE
      #include "helpers/bridges/MQTTWormholeBridge.h"
      #endif
      ```
- [ ] In `variants/heltec_v4/platformio.ini`, aggiungere `WITH_MQTT_WORMHOLE_BRIDGE=1` e
      `+<helpers/bridges/MQTTWormholeBridge.cpp>` agli environment dei due dispositivi
      (può coesistere con `WITH_MQTT_BRIDGE=1` nello stesso environment).
- [ ] **Forwarding mirato sul room server**: in `MyMesh::allowPacketForward()`
      ([MyMesh.cpp:318-325](../examples/simple_room_server/MyMesh.cpp#L318-L325)), bypassare
      **solo** il controllo `disable_fwd` per i pacchetti del wormhole — gli altri controlli
      (es. limite hop flood) restano applicati normalmente anche a loro:
      ```cpp
      bool MyMesh::allowPacketForward(const mesh::Packet *packet) {
        bool is_wormhole_packet = false;
      #if defined(WITH_MQTT_WORMHOLE_BRIDGE)
        is_wormhole_packet = wormhole.wasInjectedByWormhole(packet);
      #endif
        if (_prefs.disable_fwd && !is_wormhole_packet) return false;
        if (packet->isRouteFlood()
            && mesh::isFloodHopLimitExceeded(packet, _prefs.flood_max, _prefs.flood_max_unscoped, _prefs.flood_max_advert)) {
          return false;
        }
        return true;
      }
      ```
      **Attenzione**: la `allowPacketForward()` di `simple_repeater`
      ([MyMesh.cpp:445-470](../examples/simple_repeater/MyMesh.cpp#L445-L470)) è più estesa
      di quella del room server — ha anche il controllo di regione sconosciuta per pacchetti
      flood e il rilevamento loop. **Non copiare lo snippet del room server sopra** — va
      applicata la stessa unica modifica (bypass di `disable_fwd`), lasciando invariato tutto
      il resto:
      ```cpp
      bool MyMesh::allowPacketForward(const mesh::Packet *packet) {
        bool is_wormhole_packet = false;
      #if defined(WITH_MQTT_WORMHOLE_BRIDGE)
        is_wormhole_packet = wormhole.wasInjectedByWormhole(packet);
      #endif
        if (_prefs.disable_fwd && !is_wormhole_packet) return false;
        if (packet->isRouteFlood()
            && mesh::isFloodHopLimitExceeded(packet, _prefs.flood_max, _prefs.flood_max_unscoped, _prefs.flood_max_advert)) {
          return false;
        }
        if (packet->isRouteFlood() && recv_pkt_region == NULL) {
          MESH_DEBUG_PRINTLN("allowPacketForward: unknown transport code, or wildcard not allowed for FLOOD packet");
          return false;
        }
        if (packet->isRouteFlood() && _prefs.loop_detect != LOOP_DETECT_OFF) {
          const uint8_t* maximums;
          if (_prefs.loop_detect == LOOP_DETECT_MINIMAL) {
            maximums = max_loop_minimal;
          } else if (_prefs.loop_detect == LOOP_DETECT_MODERATE) {
            maximums = max_loop_moderate;
          } else {
            maximums = max_loop_strict;
          }
          if (isLooped(packet, maximums)) {
            MESH_DEBUG_PRINTLN("allowPacketForward: FLOOD packet loop detected!");
            return false;
          }
        }
        return true;
      }
      ```
      Su repeater, con `disable_fwd = 0` di default, `_prefs.disable_fwd && !is_wormhole_packet`
      è sempre falsa: il comportamento per tutto il traffico non-wormhole resta identico a
      oggi, byte per byte. Restano invece pienamente attivi, anche per i pacchetti del
      wormhole, il limite hop flood, il controllo regione e il rilevamento loop — un pacchetto
      del wormhole che li supera viene comunque scartato, esattamente come uno ricevuto via
      LoRA.

Stesse modifiche da applicare identiche in
[examples/simple_room_server/MyMesh.cpp](../examples/simple_room_server/MyMesh.cpp)
(`logRx()` alle righe 228-234, `begin()` a 754-757, `loop()` a 1028) — i due example non
condividono codice tra loro.

### 6. Setup del broker

- [ ] Deployare `eclipse-mosquitto:2` su Coolify (vedi "Deployment del broker").
- [ ] Esporre la porta 1883 direttamente sull'IP pubblico.
- [ ] Creare le credenziali con `mosquitto_passwd`.

### 7. Configurazione dei due dispositivi

**Su entrambi i dispositivi:**
```
set wormhole.server 167.233.95.98
set wormhole.port 1883
set wormhole.user meshcore
set wormhole.pass meshcorewormhole
set wormhole.en on
```

**Solo su Nord Italia (A):**
```
set wormhole.pub nord-to-sud
set wormhole.sub sud-to-nord
```

**Solo su Sud Italia (B):**
```
set wormhole.pub sud-to-nord
set wormhole.sub nord-to-sud
```

La configurazione `mqtt.*` dell'eventuale observer resta indipendente e non va toccata.

### 8. Test

- [ ] Con `mosquitto_sub -v` verificare che A pubblichi sul topic corretto alla ricezione di
      un pacchetto LoRA.
- [ ] Verificare che B si sottoscriva al topic giusto e decodifichi il pacchetto
      (`BRIDGE_DEBUG_PRINTLN`).
- [ ] Verificare che B ritrasmetta sulla propria LoRA (con un dispositivo di test in ascolto).
- [ ] Ripetere nella direzione opposta (B → A).
- [ ] Spegnere il broker a metà test e verificare la graceful degradation (nessun crash,
      riconnessione automatica al ripristino).

## Deployment del broker (Coolify)

- Immagine `eclipse-mosquitto:2` come servizio Docker Compose custom, volumi persistenti per
  config/data/log.
- **Esposizione**: MQTT è TCP raw, non passa dal reverse proxy Traefik/HTTPS di Coolify.
  Port mapping diretto (1883, o 8883/TLS) sull'IP pubblico del server.
- Autenticazione username/password su Mosquitto (`mosquitto_passwd`), già supportata dai
  prefs `wormhole_username`/`wormhole_password`.

### Insidie riscontrate nel deployment reale

1. **Ownership del file `passwd`**: il processo Mosquitto gira come utente `mosquitto`, non
   root. Un file creato con `docker exec <container> mosquitto_passwd -c ...` (che entra come
   root) risulta di proprietà di `root` — Mosquitto non riesce ad aprirlo, fallisce con
   `EACCES` (errno 13) e va in restart loop senza stampare errori nei log. Fix:
   ```bash
   docker exec -u root <container> chown mosquitto:mosquitto /mosquitto/config/passwd
   docker exec -u root <container> chmod 600 /mosquitto/config/passwd
   docker restart <container>
   ```
   Il warning di `mosquitto_passwd` che consiglia proprietario `root`/permessi `0700` va
   ignorato in questo container: il file deve appartenere all'utente con cui gira il
   processo (`mosquitto`).
2. **La password non si aggiorna a caldo**: `password_file` viene letto in memoria solo
   all'avvio. Dopo `mosquitto_passwd` su un container già in esecuzione serve
   `docker restart` perché la nuova password abbia effetto.
3. **Diagnosi di crash "silenziosi"**: se `docker logs` non mostra errori prima del riavvio,
   bypassare il wrapper di restart con un container usa-e-getta sugli stessi volumi:
   ```bash
   docker run --rm -v <volume-config>:/mosquitto/config --entrypoint sh eclipse-mosquitto:2 \
     -c 'ls -la /mosquitto/config/ && mosquitto -c /mosquitto/config/mosquitto.conf -v'
   ```

## Monitoraggio/debug dei pacchetti MQTT

- **`mosquitto_sub`** (pacchetto `mosquitto-clients`, `brew install mosquitto` su Mac):
  ```bash
  mosquitto_sub -h <host> -p 1883 -u <user> -P <pass> -t '#' -v
  ```
- **MQTT Explorer** (GUI, [mqtt-explorer.com](https://mqtt-explorer.com)): albero dei topic,
  JSON formattato, storico messaggi, publish manuale di messaggi di test.
- **Log del broker**: `log_type all` in `mosquitto.conf`, letti dalla tab "Logs" di Coolify.
- **Porta non esposta pubblicamente**: terminale Coolify → `mosquitto_sub -h localhost`
  dentro il container.

## Fuori scope

- Anti-loop / deduplica (`SimpleMeshTables` disponibile in `BridgeBase` se mai servisse).
- Cifratura/autenticazione applicativa del payload relayato (indipendente da TLS/auth a
  livello broker).
- Topologia multi-nodo / hub.
