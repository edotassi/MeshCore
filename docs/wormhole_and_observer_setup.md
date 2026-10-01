# Setup rapido: MQTT Observer pubblico + Wormhole Bridge

> Guida pratica, solo comandi da copiare/incollare. Per il design e il funzionamento interno
> vedi [`mqtt_wormhole_bridge.md`](./mqtt_wormhole_bridge.md); per la lista comandi generica
> del firmware vedi [`cli_commands.md`](./cli_commands.md).

Tutti i comandi vanno dati sulla console seriale del dispositivo (stesso terminale usato per
gli altri comandi admin, nessun login richiesto via seriale/USB).

## 0. Prerequisiti firmware

Il binario flashato deve essere stato compilato con i flag giusti:

| Vuoi configurare | Flag richiesto | Esempio di ambiente PlatformIO |
|---|---|---|
| Solo observer pubblico | `WITH_MQTT_BRIDGE=1` | `heltec_v4_repeater_mqtt` |
| Solo wormhole | `WITH_MQTT_WORMHOLE_BRIDGE=1` | `Heltec_v3_repeater_wormhole_noota` |
| Entrambi | `WITH_MQTT_BRIDGE=1` + `WITH_MQTT_WORMHOLE_BRIDGE=1` | `heltec_v4_repeater_mqtt`, `heltec_v4_room_server_mqtt`, `Heltec_v3_repeater_mqtt` |

Se un comando sotto risponde `unknown config: ...` il firmware probabilmente non ha il flag
corrispondente compilato.

## 1. WiFi (sempre necessario, per entrambe le funzionalità)

Observer e wormhole condividono la stessa connessione WiFi (station mode) del dispositivo.

```
set wifi.ssid <nome_rete>
set wifi.pwd <password_rete>
```

Verifica:
```
get wifi.status
```
Risposta attesa: `> connected, ip=x.x.x.x, rssi=-NN dBm`.

## 2. MQTT Observer (broker pubblico)

Pubblica telemetria/pacchetti verso un broker pubblico per dashboard/osservatori esterni
(es. CoreScope). Connessione indipendente dal wormhole, broker e credenziali separate.

```
set mqtt.server <host_broker_pubblico>
set mqtt.port 1883
set mqtt.username <utente>
set mqtt.password <password>
set mqtt.origin <nome_origine>
set mqtt.iata <codice_IATA_3_lettere>
set mqtt.status on
set mqtt.tx on
set mqtt.raw on
```

Note sui campi:
- `bridge.source` sceglie **quando** scatta la pubblicazione: `tx` (default, ad ogni
  pacchetto che il nodo inoltra/trasmette) oppure `rx` (ad ogni pacchetto ricevuto via LoRa).
  È una scelta esclusiva, non entrambe contemporaneamente.
  **Su un room server/BBS il default `tx` pubblica pochissimo**: a differenza di un
  repeater dedicato, un room server non ritrasmette il traffico mesh altrui (vedi punto 5
  più sotto), quindi in modalità `tx` l'unica cosa che `logTx()` intercetta
  ([MyMesh.cpp](../examples/bbs_room_server/MyMesh.cpp)) sono i pochi pacchetti che il nodo
  genera lui stesso (self-advert periodico, risposte dirette ai client) — `observed` resta
  quasi fermo. **Per vedere/pubblicare tutto il traffico mesh che il nodo sente** (il caso
  d'uso tipico di un "osservatore" pubblico) serve invece `rx`, che aggancia `logRx()` e
  scatta per ogni pacchetto ricevuto via radio, da chiunque:
  ```
  set bridge.source rx
  ```
- `mqtt.tx` e `mqtt.raw` sono invece due interruttori indipendenti su **dove** pubblicare
  quello stesso evento: `mqtt.tx` lo manda anche sul topic `.../packets`, `mqtt.raw` anche
  sul topic `.../raw` — puoi attivarne uno, l'altro, o entrambi. **Se sono entrambi `off`
  (default di fabbrica) il bridge si connette regolarmente al broker ma non pubblica mai un
  pacchetto** (vedi `MQTTBridge::sendPacket()` in `src/helpers/bridges/MQTTBridge.cpp`):
  `mqtt.link` risulterà comunque `connected` e `mqtt.stats` mostrerà `observed` che cresce,
  ma `published` resterà sempre a `0`.
- `mqtt.status` abilita l'heartbeat periodico di stato (intervallo via `mqtt.interval`,
  default 60000 ms) — questi publish sono conteggiati in `mqtt.stats` insieme a quelli dei
  pacchetti mesh, quindi un `published` che sale non implica da solo che stia passando
  traffico mesh: guarda `observed` per quello.

### Attivazione (richiede riavvio)

A differenza del wormhole (punto 3), l'observer **non parte da solo** anche se tutti i
campi sopra sono già configurati: va acceso esplicitamente il bridge generico, poi serve un
**riavvio**.

```
set bridge.enabled on
reboot
```

Perché il riavvio è obbligatorio: `set bridge.enabled on` dato da CLI chiama solo
`bridge.begin()` a runtime, ma l'identità del nodo (`bridge.setIdentity(...)`, usata per
client-id/topic MQTT) viene impostata **solo** dentro `MyMesh::begin()` all'avvio del
firmware, e solo se `bridge_enabled` era già `on` in quel momento — vedi
`examples/bbs_room_server/MyMesh.cpp`, blocco `if (_prefs.bridge_enabled) { ... }`. Senza
riavvio il bridge resta con identità non impostata e `MQTTBridge::loop()` ritorna subito ad
ogni ciclo senza mai tentare la connessione: `mqtt.link` resta `disconnected`
indefinitamente, anche con server/credenziali corretti al 100%.

Verifica:
```
get bridge.enabled
get mqtt.link
get mqtt.stats
```
`bridge.enabled` deve rispondere `> on`; `mqtt.link` deve rispondere `> connected`;
`mqtt.stats` mostra contatori `published`/`failed`/`observed` (`published` cresce solo se
`mqtt.tx`/`mqtt.raw` sono `on`, vedi nota sopra).

### Esempio reale usato in questo progetto

```
set mqtt.server osservatori.meshcoreitalia.it
set mqtt.port 1883
set mqtt.username osservatore
set mqtt.password meshcoreitalia!
set mqtt.origin Osservatore_Matino
set mqtt.iata ITA
set mqtt.status on
set mqtt.tx on
set bridge.source rx
set bridge.enabled on
reboot
```

## 3. Wormhole Bridge (collegamento privato punto-punto)

Collega **esattamente due** istanze di firmware via un broker MQTT privato dedicato,
re-iniettando i pacchetti ricevuti dall'altro capo nella propria mesh/LoRa. Connessione
indipendente dall'observer: broker, credenziali e topic separati.

```
set wormhole.server <host_broker_privato>
set wormhole.port 1883
set wormhole.user <utente>
set wormhole.pass <password>
```

### Topic pub/sub — incrociati tra i due nodi

Il `wormhole.pub` di un nodo deve essere identico al `wormhole.sub` dell'altro, e viceversa.

**Nodo A:**
```
set wormhole.pub a-to-b
set wormhole.sub b-to-a
```

**Nodo B:**
```
set wormhole.pub b-to-a
set wormhole.sub a-to-b
```

### Attivazione (live, nessun riavvio richiesto)

```
set wormhole.en on
```

Verifica su ciascun nodo:
```
get wormhole.en
get wormhole.server
get wormhole.pub
get wormhole.sub
get wormhole.stats
```
`wormhole.stats` deve mostrare `running=yes, connected=yes`; i contatori `sent`/`received`
salgono solo quando passa traffico LoRa reale su quel nodo.

### Esempio reale usato in questo progetto

Broker Mosquitto self-hosted su Coolify, nodi "Nord Italia" e "Sud Italia" (~1200 km):

**Sud Italia (room server):**
```
set wormhole.server 167.233.95.98
set wormhole.port 1883
set wormhole.user meshcore
set wormhole.pass meshcorewormhole
set wormhole.pub sud-to-nord
set wormhole.sub nord-to-sud
set wormhole.en on
```

**Nord Italia (repeater):**
```
set wormhole.server 167.233.95.98
set wormhole.port 1883
set wormhole.user meshcore
set wormhole.pass meshcorewormhole
set wormhole.pub nord-to-sud
set wormhole.sub sud-to-nord
set wormhole.en on
```

## 4. Checklist di verifica finale

Su ciascun nodo, in ordine:

```
get wifi.status
get bridge.enabled
get mqtt.link
get mqtt.stats
get wormhole.stats
```

Se tutto è a posto: WiFi `connected`, `bridge.enabled` `on` e `mqtt.link` `connected` (solo
se l'observer è configurato su quel nodo), `wormhole.stats` con
`running=yes, connected=yes`. `mqtt.stats`: `published` cresce solo se `mqtt.tx`/`mqtt.raw`
sono `on`; `observed` cresce ad ogni pacchetto mesh intercettato, ma solo se
`bridge.source` è coerente con quello che vuoi osservare (vedi punto 5 sotto).

## 5. Problemi comuni

- **`unknown config: ...`** → il comando non esiste in questo build: manca il flag
  `WITH_MQTT_BRIDGE`/`WITH_MQTT_WORMHOLE_BRIDGE` in compilazione (vedi tabella al punto 0).
- **`mqtt.link` resta `disconnected` nonostante server/credenziali verificati corretti**
  (es. con `mosquitto_pub -h <host> -p <porta> -u <user> -P <pass> -t test -m ping`, stessa
  rete del nodo) → quasi sempre `bridge.enabled` è ancora `off`, oppure è stato appena messo
  `on` ma manca il riavvio successivo. Vedi punto 2, "Attivazione (richiede riavvio)" — a
  differenza del wormhole, l'observer non si attiva a caldo.
- **`mqtt.link` è `connected` ma `mqtt.stats` mostra sempre `published=0`** → `mqtt.tx` e
  `mqtt.raw` sono entrambi `off` (default): il bridge osserva i pacchetti (`observed`
  cresce) ma non pubblica nulla finché non attivi almeno uno dei due (`set mqtt.tx on`
  e/o `set mqtt.raw on`, nessun riavvio richiesto per questi due).
- **Non pubblica il traffico mesh altrui, solo qualcosa ogni tanto** (`observed` quasi
  fermo, `published` che sale solo per l'heartbeat di `mqtt.status`) → `bridge.source` è
  ancora `tx` (default): su un room server/BBS questo intercetta solo i pacchetti che il
  nodo trasmette lui stesso, non il traffico mesh che semplicemente sente via radio (un
  room server non ritrasmette il traffico altrui, vedi bullet sotto). Passa a
  `set bridge.source rx` per pubblicare ogni pacchetto ricevuto via LoRa, da chiunque —
  nessun riavvio richiesto.
- **`wormhole.stats` resta `connected=no`** → controllare `wifi.status` prima (serve WiFi
  attivo), poi che `wormhole.server`/`port`/`user`/`pass` siano corretti e il broker
  raggiungibile (es. `mosquitto_pub -h <host> -u <user> -P <pass> -t test -m ping` da un PC
  sulla stessa rete/con accesso alla porta).
- **Pacchetti non attraversano il wormhole** anche con `connected=yes` → verificare che
  `wormhole.pub`/`wormhole.sub` siano effettivamente incrociati tra i due nodi (un errore
  comune è impostare lo stesso topic su entrambi i lati).
- **Il nodo ricevente non ritrasmette i pacchetti del wormhole** → su un repeater, controllare
  `get repeat` (deve essere `on`, cioè `disable_fwd=0`, salvo per i pacchetti wormhole che
  bypassano comunque questo controllo); su un room server è normale: fa da relay solo per il
  wormhole, non per il traffico locale generico.
