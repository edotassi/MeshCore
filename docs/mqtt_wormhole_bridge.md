# MQTT Wormhole Bridge — Piano di design

> Stato: **design / non ancora implementato**. Questo documento raccoglie i requisiti e le
> decisioni di design emerse finora, come base per la futura implementazione.

## Obiettivo

Ogni volta che il modulo LoRa riceve correttamente un pacchetto, inoltrarlo in modo
trasparente via internet a un'unica istanza remota "gemella" dello stesso firmware, che lo
ritrasmette sulla propria LoRa — come se le due mesh network fossero collegate tramite un
"wormhole".

**Non** è un bridge broadcast/multi-nodo come `ESPNowBridge`: è un collegamento privato,
punto-punto, tra esattamente due istanze specifiche.

## Contesto del deployment

Questa funzionalità nasce per collegare due punti a circa **1200 km di distanza** (Nord
Italia e Sud Italia), scarsamente raggiungibili tra loro dalla rete MeshCore esistente. La
topologia è **fissa e permanente a 2 nodi**: non è previsto alcun incremento futuro del
numero di dispositivi o bridge collegati. Questo vincolo è confermato ed è rilevante per
diverse decisioni di design più avanti nel documento — in particolare esclude la necessità
di anti-loop/deduplica, che servirebbe solo con una topologia a più di 2 nodi.

Entrambi i dispositivi operano in Italia, quindi sotto la stessa giurisdizione regolatoria
per la banda LoRa (ETSI EN 300 220, stessi limiti di duty cycle EU868) — nessuna
incompatibilità normativa tra i due lati del collegamento.

Concettualmente, questo caso d'uso è lo stesso di soluzioni consolidate in radioamatore per
collegare via internet due ripetitori RF troppo lontani per parlarsi direttamente (es.
EchoLink, IRLP, AllStarLink) o degli igate APRS-IS: un "hop" opportunistico via internet che
supplisce alla propagazione radio reale, non un sostituto di una copertura RF continua.

## Fattibilità

Il codebase fornisce già l'astrazione giusta per questa funzionalità:

- [`AbstractBridge`](../src/helpers/AbstractBridge.h) definisce il contratto implementato da
  ogni bridge:
  - `sendPacket(packet)` — chiamato dal core della mesh ogni volta che un pacchetto viene
    trasmesso/osservato localmente.
  - `onPacketReceived(packet)` — chiamato dal bridge per re-iniettare nella coda locale della
    mesh un pacchetto arrivato dal suo medium esterno.
- [`BridgeBase`](../src/helpers/bridges/BridgeBase.h) implementa la logica condivisa:
  checksum Fletcher-16, deduplica tramite `SimpleMeshTables`, timestamp dei log.
- Due bridge esistenti già inoltrano pacchetti bidirezionalmente su un trasporto:
  [`ESPNowBridge`](../src/helpers/bridges/ESPNowBridge.h) (broadcast via ESP-NOW) e
  `RS232Bridge` (punto-punto via seriale).
- [`MQTTBridge`](../src/helpers/bridges/MQTTBridge.h) esiste già in questo fork come
  **uplink di telemetria one-way**: pubblica JSON (con il pacchetto raw come stringa hex) su
  un broker MQTT, ma `onPacketReceived()` è attualmente un no-op — nulla viene mai
  re-iniettato nella mesh.

Conclusione: questa funzionalità è un'estensione naturale di `MQTTBridge`, trasformandolo da
uplink di telemetria one-way a relay bidirezionale punto-punto per un **peer specifico
accoppiato**, riusando il formato payload JSON/hex e la dipendenza MQTT (`PubSubClient`) già
presenti nel progetto.

## Decisioni di design

| Argomento | Decisione |
|---|---|
| Topologia | Punto-punto: esattamente 2 istanze accoppiate (non un hub multi-nodo) |
| Raggiungibilità di rete | Entrambe le istanze sono presumibilmente dietro NAT (nessun port forwarding) → serve un broker che entrambe possano raggiungere in uscita; una connessione TCP/UDP diretta tra le due non è praticabile |
| Trasporto | MQTT, estendendo l'`MQTTBridge` esistente invece di introdurre un nuovo trasporto/libreria |
| Instradamento / isolamento | Una **coppia di topic dedicata al collegamento** (uno per pubblicare i pacchetti locali, uno per sottoscrivere quelli del peer) — *non* la convenzione pubblica observer `meshcore/{IATA}/{pubkey}/raw` usata oggi per la telemetria. È questo che lo rende un "wormhole": il bridge re-inietta solo i pacchetti che arrivano sul suo topic dedicato al peer, nient'altro pubblicato sul broker |
| Formato payload | Riuso del formato JSON + hex già prodotto da `MQTTBridge::publishPacket()`. In ricezione, il campo `raw` (hex) viene decodificato e passato al `PacketManager` nello stesso modo di `ESPNowBridge::onDataRecv()` |
| Anti-loop / deduplica | Non necessaria: la topologia è fissa e permanente a 2 nodi, quindi non può crearsi un loop. `SimpleMeshTables` resta disponibile in `BridgeBase` solo se in futuro la topologia dovesse mai cambiare |
| Sicurezza / cifratura del trasporto | Nessuna cifratura applicativa per ora. Dato che il collegamento è permanente tra due dispositivi noti (non un test estemporaneo), è comunque consigliato attivare da subito username/password sul broker Mosquitto (vedi sezione deployment), invece della sola oscurità del nome del topic |
| Configurazione | Via CLI esistente (`CommonCLI`), coerente con come sono già configurati `mqtt_server` / `mqtt_port` / `mqtt_username` / `mqtt_password` |
| Broker | Mosquitto self-hosted, deployato come servizio Docker sull'istanza Coolify già disponibile dell'utente (server pubblico già presente, nessun costo aggiuntivo) |

## Deployment del broker (Coolify)

- Deployare l'immagine ufficiale `eclipse-mosquitto` come servizio custom su Coolify
  (docker-compose), con volume persistente per config e password file.
- **Esposizione**: MQTT è un protocollo TCP raw, non HTTP, quindi non passa attraverso il
  reverse proxy automatico Traefik/HTTPS di Coolify come i servizi web. Due opzioni:
  1. **Port mapping diretto** (consigliato per questo caso d'uso): pubblicare la 1883 (o
     8883 per TLS) direttamente sull'IP pubblico del server tramite le impostazioni di
     esposizione porte di Coolify. Più semplice, nessun lavoro aggiuntivo sul protocollo.
  2. **MQTT su WebSocket**: abilitare il listener `protocol websockets` di Mosquitto in modo
     che il traffico passi dal normale proxy HTTP(S) di Coolify ottenendo TLS automatico via
     Let's Encrypt su un sottodominio. Più lavoro di setup, evita di aprire una porta non
     standard.
- **Consigliato anche se per la logica di relay è stata scelta "nessuna sicurezza"**:
  abilitare autenticazione username/password su Mosquitto (`mosquitto_passwd`). Non costa
  nulla in più da implementare dato che `MQTTBridge` supporta già `mqtt_username` /
  `mqtt_password`, ed evita di lasciare un broker esposto su internet completamente aperto.

## Monitoraggio/debug dei pacchetti MQTT

Strumenti per osservare il traffico sul broker durante lo sviluppo e la diagnostica:

- **`mosquitto_sub` da riga di comando** (pacchetto `mosquitto-clients`, `brew install
  mosquitto` su Mac): il modo più rapido.
  ```bash
  mosquitto_sub -h <tuo-server> -p 1883 -u <user> -P <pass> -t '#' -v
  ```
  `-t '#'` sottoscrive tutti i topic (utile in debug generale; una volta definiti i topic
  del wormhole si può restringere, es. `-t 'meshcore/wormhole/#'`), `-v` stampa
  `topic payload` per ogni messaggio.
- **MQTT Explorer** (GUI gratuita, Mac/Win/Linux, [mqtt-explorer.com](https://mqtt-explorer.com)):
  mostra un albero dei topic con valori live, JSON già formattato, storico messaggi per
  topic, e permette di pubblicare messaggi di test manualmente — utile per simulare un
  pacchetto del peer senza dover accendere il secondo dispositivo fisico.
- **Log del broker lato server**: impostare `log_type all` nel `mosquitto.conf` del
  container e leggere i log dalla tab "Logs" di Coolify. Utile per diagnosticare a basso
  livello connessioni/disconnessioni e fallimenti di autenticazione, senza dover aprire una
  connessione client esterna.
- **Se la porta non è esposta pubblicamente**: usare la funzione "Execute Command"/terminale
  di Coolify per entrare nel container Mosquitto ed eseguire `mosquitto_sub -h localhost` in
  locale.

## Nota: identità e path dei pacchetti attraverso il wormhole

Chi riceve dall'altra parte via LoRA **non nota nulla di anomalo**, ma con una precisazione
importante su cosa significa "identico":

- Il **payload** del pacchetto (`Packet::payload[]` in [Packet.h](../src/Packet.h)) — che
  contiene MAC/firma, dati cifrati e, per gli ADVERT, la chiave pubblica dell'identità del
  mittente — è completamente separato dall'header di routing (`header`, `path[]`) e non
  viene mai toccato dal layer di forwarding della mesh. L'identità/firma del mittente
  originale è quindi preservata al 100%, indipendentemente dal transport (LoRA, ESP-NOW,
  seriale o MQTT wormhole).
- Il bridge mette il pacchetto in coda tramite `BridgeBase::handleReceivedPacket` →
  `_mgr->queueInbound(...)` ([BridgeBase.cpp:45](../src/helpers/bridges/BridgeBase.cpp#L45)),
  esattamente come se fosse arrivato via LoRA: entra nella stessa pipeline di elaborazione
  di un pacchetto ricevuto via radio.
- L'header di routing **non** è ritrasmesso byte-per-byte identico, ed è corretto così:
  come ogni altro nodo che inoltra un pacchetto, l'istanza che lo ritrasmette sulla propria
  LoRA aggiunge il proprio hash al `path` per le route FLOOD
  ([Mesh.cpp:348-349](../src/Mesh.cpp#L348-L349)), oppure consuma il proprio hop
  rimuovendolo dal `path` per le route DIRECT
  ([Mesh.cpp:335-340](../src/Mesh.cpp#L335-L340)) — esattamente come farebbe un ripetitore
  fisico che ha ricevuto il pacchetto via radio.

Il risultato è che il wormhole è trasparente nel senso corretto: chi riceve vede un
pacchetto con l'identità/firma originale intatta, che sembra essere passato per un hop
mesh legittimo attraverso il nodo remoto — non un'anomalia rilevabile, ma un normale hop
multi-hop.

## Piano di implementazione

Checklist dettagliata, file per file, con riferimenti esatti al codice esistente da cui
partire. Pensata per essere seguita direttamente in fase di sviluppo, in ordine.

### 1. `src/helpers/CommonCLI.h` — nuovi campi prefs

Nella sezione `// MQTT bridge settings (MQTT only)` (dopo `mqtt_interval`, riga ~69),
aggiungere:

```cpp
uint8_t mqtt_wormhole_enabled = 0;      // boolean: abilita il relay bidirezionale
char mqtt_wormhole_pub_topic[64];       // topic su cui pubblicare i pacchetti locali
char mqtt_wormhole_sub_topic[64];       // topic del peer a cui iscriversi per l'injection
```

- [ ] Aggiungere i 3 campi dopo `mqtt_interval` in `NodePrefs`.
- [ ] Registrarli nella nested class `MqttPrefs::structure()` (riga ~185-197), accanto agli
      altri `def("...")` esistenti:
      ```cpp
      def("wh_en", _parent->mqtt_wormhole_enabled);
      def("wh_pub", _parent->mqtt_wormhole_pub_topic, sizeof(_parent->mqtt_wormhole_pub_topic));
      def("wh_sub", _parent->mqtt_wormhole_sub_topic, sizeof(_parent->mqtt_wormhole_sub_topic));
      ```
- [ ] Inizializzare le stringhe nel costruttore di `NodePrefs` (riga ~232-236, accanto a
      `mqtt_iata[0] = 0;`):
      ```cpp
      mqtt_wormhole_pub_topic[0] = 0;
      mqtt_wormhole_sub_topic[0] = 0;
      ```

*Nota*: niente di nuovo per la connessione al broker — si riusano `mqtt_server` /
`mqtt_port` / `mqtt_username` / `mqtt_password` già esistenti.

### 2. `src/helpers/CommonCLI.cpp` — comandi CLI `set`/`get`

Nel blocco `#ifdef WITH_MQTT_BRIDGE` dei comandi `set` (dopo `mqtt.interval`, riga ~799),
seguendo esattamente il pattern di `mqtt.status`/`mqtt.server`:

- [ ] `set mqtt.wh_en on|off`
      ```cpp
      } else if (memcmp(config, "mqtt.wh_en ", 11) == 0) {
        _prefs->mqtt_wormhole_enabled = memcmp(&config[11], "on", 2) == 0;
        savePrefs();
        strcpy(reply, "OK");
      ```
- [ ] `set mqtt.wh_pub <topic>`
      ```cpp
      } else if (memcmp(config, "mqtt.wh_pub ", 12) == 0) {
        StrHelper::strncpy(_prefs->mqtt_wormhole_pub_topic, &config[12], sizeof(_prefs->mqtt_wormhole_pub_topic));
        savePrefs();
        strcpy(reply, "OK");
      ```
- [ ] `set mqtt.wh_sub <topic>` (stesso pattern, su `mqtt_wormhole_sub_topic`).

Nel blocco `get` (dopo `mqtt.interval`, riga ~1027):

- [ ] `get mqtt.wh_en` → `sprintf(reply, "> %s", _prefs->mqtt_wormhole_enabled ? "on" : "off");`
- [ ] `get mqtt.wh_pub` → `sprintf(reply, "> %s", _prefs->mqtt_wormhole_pub_topic);`
- [ ] `get mqtt.wh_sub` → `sprintf(reply, "> %s", _prefs->mqtt_wormhole_sub_topic);`

### 3. `src/helpers/bridges/MQTTBridge.h` — nuovi membri

- [ ] Aggiungere un puntatore statico all'istanza (stesso pattern di
      `ESPNowBridge::_instance`, [ESPNowBridge.h:44](../src/helpers/bridges/ESPNowBridge.h#L44)),
      necessario perché `PubSubClient::setCallback()` accetta solo una funzione libera, non un
      metodo di istanza:
      ```cpp
      static MQTTBridge* _instance;
      static void mqttMessageCallback(char* topic, uint8_t* payload, unsigned int length);
      ```
- [ ] Aggiungere i metodi di gestione del messaggio in ingresso:
      ```cpp
      void onMqttMessage(const char* topic, const uint8_t* payload, unsigned int length);
      bool injectPacketFromJson(const uint8_t* json, unsigned int length);
      ```
- [ ] Modificare la firma di `onPacketReceived()` (già dichiarata in `AbstractBridge`): non
      cambia, ma il corpo non sarà più un no-op (vedi punto 4).

### 4. `src/helpers/bridges/MQTTBridge.cpp` — implementazione

- [ ] Nel costruttore, aggiungere `_instance = this;` (come
      [ESPNowBridge.cpp:26](../src/helpers/bridges/ESPNowBridge.cpp#L26)).
- [ ] In `begin()`, registrare la callback una sola volta: `_mqtt_client.setCallback(mqttMessageCallback);`
- [ ] In `ensureMqtt()`, subito dopo una connessione riuscita (dove oggi c'è solo
      `_reconnect_count++`), se `_prefs->mqtt_wormhole_enabled` e
      `_prefs->mqtt_wormhole_sub_topic[0] != 0`: `_mqtt_client.subscribe(_prefs->mqtt_wormhole_sub_topic);`
      (le sottoscrizioni MQTT non sopravvivono a una riconnessione, va rifatta ogni volta).
- [ ] Implementare `mqttMessageCallback()` come wrapper statico che chiama
      `_instance->onMqttMessage(topic, payload, length)` (stesso pattern di
      `ESPNowBridge::recv_cb`, [ESPNowBridge.cpp:12-16](../src/helpers/bridges/ESPNowBridge.cpp#L12-L16)).
- [ ] Implementare `onMqttMessage()`:
      - Guardia: se `!_prefs->mqtt_wormhole_enabled`, return.
      - Chiama `injectPacketFromJson(payload, length)`.
- [ ] Implementare `injectPacketFromJson()` — **senza introdurre una dipendenza JSON**
      (il progetto non usa ArduinoJson, e qui serve solo estrarre un campo): cercare la
      sottostringa `"raw":"` nel buffer, leggere i caratteri hex fino alla `"` di chiusura in
      un buffer locale (`char hex[561]`, stessa dimensione usata in
      `publishPacket()`/`raw_hex`), poi:
      ```cpp
      uint8_t decoded[280];  // stessa dimensione di raw_bytes in publishPacket()
      if (!mesh::Utils::fromHex(decoded, sizeof(decoded), hex)) return false;

      mesh::Packet* pkt = _mgr->allocNew();
      if (!pkt) return false;

      if (pkt->readFrom(decoded, decoded_len)) {
        onPacketReceived(pkt);   // -> handleReceivedPacket() -> _mgr->queueInbound(...)
        return true;
      }
      _mgr->free(pkt);
      return false;
      ```
      (`mesh::Utils::fromHex` è già dichiarato in [Utils.h:67](../src/Utils.h#L67) e usato
      altrove nel progetto; `decoded_len` si ricava da `strlen(hex) / 2`).
- [ ] Sostituire il corpo (oggi no-op) di `onPacketReceived()`:
      ```cpp
      void MQTTBridge::onPacketReceived(mesh::Packet* packet) {
        handleReceivedPacket(packet);   // da BridgeBase, come ESPNowBridge::onPacketReceived
      }
      ```
- [ ] In `sendPacket()` (dove oggi pubblica su `"raw"`/`"packets"`), se
      `_prefs->mqtt_wormhole_enabled` e `_prefs->mqtt_wormhole_pub_topic[0] != 0`, pubblicare lo
      stesso payload JSON anche sul topic wormhole dedicato (riusando `publishPacket()` con
      `topic_suffix` sostituito da `_prefs->mqtt_wormhole_pub_topic` — richiede una piccola
      variante di `buildTopic()`/`publishPacket()` che accetti un topic assoluto invece di un
      suffisso, dato che il topic wormhole non segue la convenzione
      `meshcore/{IATA}/{pubkey}/...`).

### 5. Wiring esistente — nessuna modifica richiesta

Il resto della catena funziona già senza toccare altro codice:

- `MyMesh::logRx()` ([MyMesh.cpp:490-497](../examples/simple_repeater/MyMesh.cpp#L490-L497))
  chiama già `bridge.sendPacket(pkt)` quando `bridge_pkt_src == 1` (cioè quando il pref
  `bridge.source` è impostato su `rx`) — è già l'hook "ogni volta che il modulo LoRa riceve
  correttamente un pacchetto" richiesto dall'obiettivo del progetto.
- `bridge.begin()` / `bridge.setIdentity()` sono già chiamati in `MyMesh::begin()`
  ([MyMesh.cpp:1001-1007](../examples/simple_repeater/MyMesh.cpp#L1001-L1007)), guardati da
  `_prefs.bridge_enabled`.
- `bridge.loop()` è già chiamato nel loop principale
  ([MyMesh.cpp:1431](../examples/simple_repeater/MyMesh.cpp#L1431)).
- Il flag di build `WITH_MQTT_BRIDGE` e il sorgente `MQTTBridge.cpp` sono già cablati in
  [variants/heltec_v4/platformio.ini](../variants/heltec_v4/platformio.ini).

### 6. Setup del broker

- [ ] Deployare `eclipse-mosquitto` su Coolify (vedi sezione "Deployment del broker").
- [ ] Esporre la porta 1883 (o 8883/TLS) direttamente sull'IP pubblico.
- [ ] Creare le credenziali con `mosquitto_passwd`.

### 7. Configurazione dei due dispositivi

Su ogni istanza, via CLI seriale/BLE:
```
set mqtt.server <ip-broker>
set mqtt.port 1883
set mqtt.username <user>
set mqtt.password <pass>
set mqtt.wh_en on
set bridge.source rx
set bridge.en on
```
Sul Nord Italia (A): `set mqtt.wh_pub topic/nord-to-sud` e `set mqtt.wh_sub topic/sud-to-nord`.
Sul Sud Italia (B): topic invertiti — `set mqtt.wh_pub topic/sud-to-nord` e
`set mqtt.wh_sub topic/nord-to-sud`.

### 8. Test

- [ ] Con `mosquitto_sub -v` (vedi sezione debug) verificare che A pubblichi sul topic
      corretto quando riceve un pacchetto LoRA.
- [ ] Verificare che B si sottoscriva al topic giusto e stampi in log (`BRIDGE_DEBUG_PRINTLN`)
      la ricezione/decodifica del pacchetto.
- [ ] Verificare che B ritrasmetta effettivamente sulla propria LoRA (con un secondo
      dispositivo di test in ascolto vicino a B).
- [ ] Ripetere nella direzione opposta (B → A).
- [ ] Spegnere il broker a metà test e verificare il comportamento di graceful degradation
      (nessun crash, riconnessione automatica al ripristino).

## Rischi e controindicazioni

Valutazione calata sul contesto reale del deployment (2 dispositivi fissi, Nord/Sud Italia,
nessuna crescita prevista della topologia — vedi "Contesto del deployment").

**Non più rilevanti in questo scenario**
- *Loop/amplificazione da crescita della topologia*: risolto in radice dalla topologia fissa
  a 2 nodi confermata dall'utente — non serve deduplica/anti-loop.
- *Incompatibilità normativa tra i due lati*: entrambi i dispositivi sono in Italia, stessa
  banda e stessi limiti di duty cycle (EU868, ETSI EN 300 220).
- *Duty cycle radio da volume di traffico*: due punti "scarsamente collegati" implicano
  presumibilmente un traffico reale basso; il rischio di sforare i limiti legali di airtime
  locale è più teorico che pratico in questo caso specifico.
- *Rottura delle euristiche di topologia/hop-distance*: tecnicamente vero (un hop di 1200 km
  non si comporta come un hop radio), ma qui non è un effetto collaterale indesiderato — è
  lo scopo dichiarato del collegamento, con lo stesso principio di EchoLink/IRLP/AllStarLink
  o degli igate APRS-IS in radioamatore.
- *Consenso/etichetta verso altri partecipanti*: attenuato dal fatto che l'obiettivo è
  proprio ricongiungere due porzioni di mesh scarsamente collegate; resta comunque buona
  norma essere trasparenti con chi usa le due mesh locali sull'esistenza del collegamento.

**Restano pienamente validi**
- **Internet come single point of failure**: è il rischio residuo più importante. Una mesh
  LoRA è per design resiliente e autonoma da infrastruttura esterna (self-healing via RF).
  Il wormhole introduce una dipendenza da rete/ISP, server Coolify e broker Mosquitto: se uno
  di questi cade, i due punti tornano isolati esattamente come prima del bridge. Va bene se
  è chiaro a tutti che si tratta di un collegamento opportunistico e non di una garanzia di
  connettività, va gestito con attenzione se qualcuno inizia a farci affidamento come se
  fosse permanente.
- **Nessuna validazione in ingresso**: qualunque cosa arrivi sul topic MQTT dedicato viene
  iniettata ciecamente nella mesh fisica locale (advert falsificati, spam, flood). Essendo un
  collegamento permanente tra due dispositivi noti (bersaglio stabile e prevedibile nel
  tempo, non un test estemporaneo), attivare da subito autenticazione sul broker è più
  importante che in uno scenario usa-e-getta.
- **Superficie d'attacco aggiuntiva**: il broker (anche self-hosted su Coolify) è un servizio
  esposto su internet in più da mantenere/patchare, e diventa un single point of
  compromissione che prima non esisteva.
- **Metadata leakage**: ogni pacchetto locale (contenuto raw, SNR/RSSI, timestamp, identità)
  viene pubblicato quasi in chiaro sul broker. Lo scope è contenuto e prevedibile (traffico
  di esattamente 2 dispositivi fissi), ma è comunque un'esposizione continuativa nel tempo,
  diversa dal perimetro naturale di una mesh isolata fisicamente dalla portata radio.
- **Guasti silenziosi**: un'interruzione del broker/WiFi rompe l'assunzione "mirror
  identico" senza alcun segnale visibile agli utenti finali oltre i log di debug (perdita,
  duplicazione o ritardo dei pacchetti).
- **Consumo energetico**: mantenere WiFi + connessione MQTT persistente è un carico non
  trascurabile se uno dei due dispositivi è a batteria/solare invece che alimentato da rete
  fissa — da verificare per entrambi i siti.

## Rimandato esplicitamente (lavoro futuro)

- Cifratura/autenticazione applicativa del payload relayato (indipendente dalla sicurezza a
  livello broker — TLS + username/password su Mosquitto, quella è consigliata da subito, vedi
  sopra).
- Topologia multi-nodo / hub: esplicitamente fuori scope per questo design, non solo
  rimandata — se mai richiesta in futuro andrebbe rivalutata da capo (servirebbe
  anti-loop/deduplica).
