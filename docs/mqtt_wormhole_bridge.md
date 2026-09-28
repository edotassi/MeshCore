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
| Anti-loop / deduplica | Nessuna per ora (rimandata — `SimpleMeshTables` è già disponibile in `BridgeBase` se servisse in futuro) |
| Sicurezza / cifratura del trasporto | Nessuna per ora (rimandata). L'unica protezione iniziale è l'oscurità del nome del topic dedicato |
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

## Schema di implementazione (non ancora realizzato)

1. **Prefs / CLI**: aggiungere la configurazione per il collegamento wormhole — topic di
   pubblicazione, topic di sottoscrizione (oppure un singolo "peer id" da cui entrambi i lati
   derivano la propria coppia di topic), riusando i prefs esistenti `mqtt_server` /
   `mqtt_port` / `mqtt_username` / `mqtt_password` per la connessione al broker. Esporre
   tramite nuovi comandi `CommonCLI`, seguendo il pattern delle impostazioni `mqtt.*`
   esistenti.
2. **Modifiche a MQTTBridge**:
   - Sottoscrivere il topic del peer configurato una volta connessi (in
     `loop()`/`ensureMqtt()`).
   - Implementare un vero `onPacketReceived()` / callback sui messaggi MQTT: parsare il JSON
     in arrivo, estrarre il campo hex `raw`, decodificarlo, allocare un pacchetto tramite
     `PacketManager`, chiamare `packet->readFrom(...)` e metterlo in coda per l'elaborazione
     locale della mesh — rispecchiando `ESPNowBridge::onDataRecv()`.
   - Mantenere separata la pubblicazione sul topic observer esistente dal nuovo topic di
     pubblicazione "wormhole" dedicato (oppure riusare lo stesso payload pubblicato su
     entrambi, se preferito).
3. **Setup di Mosquitto su Coolify**: deployare il broker, esporre la porta, creare le
   credenziali.
4. **Test**: due istanze, ciascuna puntata sul broker condiviso con i topic invertiti (A
   pubblica sul topic X / sottoscrive Y, B pubblica su Y / sottoscrive X), verificare che un
   pacchetto ricevuto dalla LoRA di A venga ritrasmesso identicamente dalla radio di B.

## Rimandato esplicitamente (lavoro futuro)

- Anti-loop / deduplica (`SimpleMeshTables`), necessaria se la topologia del collegamento
  dovesse mai crescere oltre una rigida coppia 1:1.
- Cifratura/autenticazione del trasporto del payload relayato (la connessione MQTT può già
  essere messa in sicurezza a livello broker con TLS + username/password, indipendentemente
  da questo).
- Topologia multi-nodo / hub (più di 2 istanze che condividono un unico canale).
