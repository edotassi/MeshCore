## About MeshCore

MeshCore is a lightweight, portable C++ library that enables multi-hop packet routing for embedded projects using LoRa and other packet radios. It is designed for developers who want to create resilient, decentralized communication networks that work without the internet.

## 🔍 What is MeshCore?

MeshCore now supports a range of LoRa devices, allowing for easy flashing without the need to compile firmware manually. Users can flash a pre-built binary using tools like Adafruit ESPTool and interact with the network through a serial console.
MeshCore provides the ability to create wireless mesh networks, similar to Meshtastic and Reticulum but with a focus on lightweight multi-hop packet routing for embedded projects. Unlike Meshtastic, which is tailored for casual LoRa communication, or Reticulum, which offers advanced networking, MeshCore balances simplicity with scalability, making it ideal for custom embedded solutions, where devices (nodes) can communicate over long distances by relaying messages through intermediate nodes. This is especially useful in off-grid, emergency, or tactical situations where traditional communication infrastructure is unavailable.

## ⚡ Key Features

* Multi-Hop Packet Routing
  * Devices can forward messages across multiple nodes, extending range beyond a single radio's reach.
  * Supports up to a configurable number of hops to balance network efficiency and prevent excessive traffic.
  * Nodes use fixed roles where "Companion" nodes are not repeating messages at all to prevent adverse routing paths from being used.
* Supports LoRa Radios – Works with Heltec, RAK Wireless, and other LoRa-based hardware.
* Decentralized & Resilient – No central server or internet required; the network is self-healing.
* Low Power Consumption – Ideal for battery-powered or solar-powered devices.
* Simple to Deploy – Pre-built example applications make it easy to get started.

## 🎯 What Can You Use MeshCore For?

* Off-Grid Communication: Stay connected even in remote areas.
* Emergency Response & Disaster Recovery: Set up instant networks where infrastructure is down.
* Outdoor Activities: Hiking, camping, and adventure racing communication.
* Tactical & Security Applications: Military, law enforcement, and private security use cases.
* IoT & Sensor Networks: Collect data from remote sensors and relay it back to a central location.

## 🚀 How to Get Started

- Watch the [MeshCore QuickStart Playlist](https://www.youtube.com/watch?v=iaFltojJrAc&list=PLshzThxhw4O4WU_iZo3NmNZOv6KMrUuF9) by The Comms Channel
- Watch the [MeshCore Technical Presentation](https://www.youtube.com/watch?v=OwmkVkZQTf4) by Liam Cottle.
- Read through our [Frequently Asked Questions](./docs/faq.md) and [Documentation](https://docs.meshcore.io).
- Flash the MeshCore firmware on a supported device.
- Connect with a supported client.

For developers:

- Install [PlatformIO](https://docs.platformio.org) in [Visual Studio Code](https://code.visualstudio.com).
- Clone and open the MeshCore repository in Visual Studio Code.
- See the example applications you can modify and run:
  - [Companion Radio](./examples/companion_radio) - For use with an external chat app, over BLE, USB or Wi-Fi.
  - [KISS Modem](./examples/kiss_modem) - Serial KISS protocol bridge for host applications. ([protocol docs](./docs/kiss_modem_protocol.md))
  - [Simple Repeater](./examples/simple_repeater) - Extends network coverage by relaying messages.
  - [Simple Room Server](./examples/simple_room_server) - A simple BBS server for shared Posts.
  - [Simple Secure Chat](./examples/simple_secure_chat) - Secure terminal based text communication between devices.
  - [Simple Sensor](./examples/simple_sensor) - Remote sensor node with telemetry and alerting.

The Simple Secure Chat example can be interacted with through the Serial Monitor in Visual Studio Code, or with a Serial USB Terminal on Android.

## ⚡️ MeshCore Flasher

We have prebuilt firmware ready to flash on supported devices.

- Launch https://meshcore.io/flasher
- Select a supported device
- Flash one of the firmware types:
  - Companion, Repeater or Room Server
- Once flashing is complete, you can connect with one of the MeshCore clients below.

## 📱 MeshCore Clients

**Companion Firmware**

The companion firmware can be connected to via BLE, USB or Wi-Fi depending on the firmware type you flashed.

- Web: https://app.meshcore.nz
- Android: https://play.google.com/store/apps/details?id=com.liamcottle.meshcore.android
- iOS: https://apps.apple.com/us/app/meshcore/id6742354151?platform=iphone
- NodeJS: https://github.com/liamcottle/meshcore.js
- Python: https://github.com/fdlamotte/meshcore-cli

**Repeater and Room Server Firmware**

The repeater and room server firmware can be set up via USB in the web config tool.

- https://config.meshcore.io

They can also be managed via LoRa in the mobile app by using the Remote Management feature.

## 📨 Store & Forward (Repeater, custom addition)

This fork's `simple_repeater` firmware (`v1.17.1-1-sf`) adds an optional store-and-forward feature for companion nodes that are frequently offline: the repeater holds direct messages and channel messages in a RAM queue and replays them once a configured companion's advert is heard again.

**How it works**

- You configure the repeater with a list of "companion" pubkey prefixes (2+ bytes each). A companion must be *learned* — i.e. the repeater must hear at least one advert from it — before its direct messages can be recognized and queued, since the wire protocol only carries a 1-byte destination hash on direct messages; the full pubkey is only visible in adverts.
- Direct messages (`TXT_MSG`) addressed to a known companion are queued as a safety-net copy, regardless of whether normal flood/direct forwarding also succeeds. Messages sent *by* a companion are never queued (it's clearly online if it just transmitted).
- Channel messages (`GRP_TXT`) have no per-node destination, so all of them are queued unconditionally and replayed in full whenever any allow-listed companion's advert is seen.
- Adverts from *non*-companion nodes are also cached — one entry per unique identity (deduped by pubkey, refreshed in place on repeat sightings) — and all of them are replayed alongside messages when a companion reappears.
- The message queue (`SF_MAX_QUEUE`, default 500 entries) and the advert cache (`SF_MAX_ADVERTS`, default 64 entries) are each a fixed-size ring buffer — once full, the oldest entry is silently dropped to make room for new ones.
- Every queued item has a time-to-live; entries older than the TTL are dropped rather than replayed (checked opportunistically on new arrivals and again at replay time). Defaults: 24h for messages, 1h for adverts — both adjustable at runtime.
- Everything is RAM-only except the `enabled` flag, companion list, and TTLs, which persist across reboots.

**New CLI commands** (repeater firmware, same console as other admin commands):

| Command | Effect |
|---|---|
| `storeforward enable` / `storeforward disable` | Turns the feature on/off (persisted) |
| `storeforward keyids <hex1,hex2,...>` | Sets the companion allow-list — comma-separated pubkey prefixes, 2–8 bytes each |
| `storeforward keyids get` | Reads back the configured companion list (`*` marks a companion already seen/learned) |
| `storeforward ttl msg <seconds>` | Sets the message TTL (10s – ~46 days), persisted |
| `storeforward ttl advert <seconds>` | Sets the advert-cache TTL (10s – ~46 days), persisted |
| `storeforward ttl get` | Reads back both TTLs, in seconds |
| `storeforward stats` | Shows enabled state, companion count, message/advert queue size and capacity, and lifetime queued/replayed/evicted/expired counts |
| `storeforward reset` | Clears the in-memory message queue and advert cache immediately (companion list, TTLs, and enabled flag untouched) |

This is a repeater-only addition — it doesn't touch `companion_radio`, `simple_room_server`, or `simple_sensor`.

## 📊 WiFi Dashboard (Repeater, custom addition)

This fork's `simple_repeater` firmware (`v1.17.1-12-sf`) also adds an optional on-demand WiFi status dashboard, built for battery/solar-powered repeaters: WiFi is off by default and only switches on for a bounded window via CLI, so normal operation pays no WiFi power cost.

**How it works**

- Battery, radio (RSSI/SNR/noise floor), and packet-rate stats are sampled periodically into a fixed-capacity ring buffer held in PSRAM (not flash), so normal sampling costs zero flash writes. The buffer is snapshotted to a single SPIFFS file only every *flush interval*, and only the slots actually in use — not the full fixed-size buffer — keeping flash wear negligible. History (and its logical timestamp) survives a reboot, since the snapshot is reloaded and the sample clock picks up counting from where it left off rather than restarting at zero.
- `wifidash on` starts an open WiFi AP (`MeshCore-Dash`) and a lightweight web server serving a single self-contained HTML page — no external JS/CSS, since the AP itself has no internet access. It auto-switches back off after a configurable timeout (default 10 minutes) if left unattended; `wifidash off` shuts it down immediately.
- The dashboard page shows: current battery/RSSI/SNR/noise-floor/packet stats, free heap, MCU temperature, last reset reason, and history usage; straight-line (deliberately not smoothed) charts for battery, RSSI/SNR/noise floor, and packet rates, each with "time ago" labels on the x-axis; a read-only dump of every repeater setting (radio, bridge, GPS, repeat, power), with the admin/guest passwords and bridge secret always redacted; and a neighbours table (ID, SNR, time since heard, location if shared) with a button that triggers the same discovery request as the `discover.neighbors` CLI command.
- The page itself can optionally be gated behind HTTP Basic Auth (`wifidash pass`) — the AP is open at the network level (anyone in range can join the WiFi), so this is the only access control on the page's contents.

**New CLI commands** (repeater firmware, same console as other admin commands):

| Command | Effect |
|---|---|
| `wifidash on [timeout_minutes]` | Turns on the AP + dashboard page; reply includes the AP's IP. Omit the timeout to use the persisted default |
| `wifidash off` | Immediate manual shutdown |
| `wifidash status` | Reports on/off, IP, and time remaining before auto-off |
| `wifidash timeout <minutes>` | Sets the persisted default auto-off timeout (1–360 min) for future `on` calls |
| `wifidash pass <password>` | Sets an HTTP Basic Auth password on the dashboard page (≥8 chars), persisted |
| `wifidash pass` | Clears the password — page becomes open |
| `wifidash interval <seconds>` | Sets the history sampling interval (30–3600s), persisted |
| `wifidash flushint <seconds>` | Sets the history flash-flush interval (60–86400s), persisted |
| `wifidash reset` | Clears the battery/stats history immediately (wifidash config itself untouched) |

This is a repeater-only addition, behind the `WITH_WIFI_DASHBOARD` build flag (see the `heltec_v4_repeater_wifidash` PlatformIO env) — it doesn't touch `companion_radio`, `simple_room_server`, or `simple_sensor`, and default repeater builds are unaffected when the flag is off.

## 🌀 MQTT Wormhole Bridge (Repeater/Room Server, custom addition)

A private, point-to-point relay between **exactly two paired instances** of this firmware over their own MQTT broker — e.g. linking two nodes hundreds of km apart that have no LoRa path between them. Unlike the read-only MQTT observer bridge (`WITH_MQTT_BRIDGE`, which only reports mesh activity to a public broker for dashboards), the wormhole re-injects packets back into the mesh on the receiving end, so it behaves like a long-haul LoRa link rather than a read-only feed. Full design notes: [docs/mqtt_wormhole_bridge.md](./docs/mqtt_wormhole_bridge.md).

**How it works**

- Every LoRa packet this node receives (`logRx`, never on transmit) is published as-is (same JSON+hex format as the MQTT observer bridge) to its own publish topic. Whatever arrives on its subscribe topic is decoded and re-injected into the local mesh, to be retransmitted over its own LoRa.
- Runs over a second, fully independent MQTT connection (own broker, credentials and topic pair) — separate from the observer bridge, which keeps publishing to the public broker unaffected. Both can be enabled at the same time, on the same device.
- On the receiving node, only the `disable_fwd` check is bypassed for packets that came in through the wormhole, so a room server (which doesn't forward normal mesh traffic) still relays wormhole traffic specifically; every other check (flood hop limit, loop detection, region) still applies normally.
- No anti-loop/dedup beyond the mesh's normal duplicate-packet check, and no payload encryption of its own — only broker username/password. This is intentional: it's built for a fixed, permanent 2-node link, not a multi-node topology.

**New CLI commands** (same console as other admin commands):

| Command | Effect |
|---|---|
| `set wormhole.en on\|off` | Enables/disables the bridge, live (no reboot needed), persisted |
| `set wormhole.server <host>` | Broker hostname/IP |
| `set wormhole.port <port>` | Broker port (1-65535) |
| `set wormhole.user <user>` | Broker username |
| `set wormhole.pass <pass>` | Broker password |
| `set wormhole.pub <topic>` | Topic this node publishes received packets to |
| `set wormhole.sub <topic>` | Topic this node subscribes to for packets to re-inject |
| `get wormhole.en` / `.server` / `.port` / `.user` / `.pass` / `.pub` / `.sub` | Reads back each setting (password masked as `********` if set) |

On the two paired nodes, `wormhole.pub` on one must match `wormhole.sub` on the other, and vice versa (crossed pair), while `wormhole.server`/`.port`/`.user`/`.pass` are identical on both since they point at the same broker.

`get wormhole.stats` reports live counters (`running`, `connected`, packets `sent`/`received` through the wormhole) — useful for checking the link is alive without external tooling.

**On-device status (OLED)**: on builds with a display (e.g. `heltec_v4_repeater_mqtt` / `heltec_v4_room_server_mqtt`), the bottom two rows of the home screen alternate every 5 seconds between the MQTT observer status (`MQTT:OK/off <published>/<failed>`, packets/clients) and the wormhole status (`Wormhole:OK/connecting/off`, `WH Tx:<sent> Rx:<received>`) — so both links can be checked at a glance in the field without a laptop.

This is behind the `WITH_MQTT_WORMHOLE_BRIDGE` build flag — it can be combined with `WITH_MQTT_BRIDGE` in the same build (both connections run independently) or used on its own (wormhole only, no public observer at all). It doesn't touch `companion_radio`, `simple_secure_chat`, or `simple_sensor`. `WITH_MQTT_BRIDGE`/`WITH_MQTT_WORMHOLE_BRIDGE` also control which CLI commands exist (`wifi.*`/`mqtt.*`/`wormhole.*`) and what the display shows — either flag alone is enough to unlock the extended status screen (WiFi/uptime + MQTT-or-wormhole row), and `wifi.ssid`/`wifi.pwd`/`wifi.status` work under either flag since both bridges share the same WiFi radio.

PlatformIO envs with this flag:
- `heltec_v4_repeater_mqtt` / `heltec_v4_room_server_mqtt` — observer + wormhole, Heltec V4 (has PSRAM)
- `Heltec_v3_repeater_mqtt` — observer + wormhole, Heltec V3
- `Heltec_v3_repeater_mqtt_noota` — same, without the WiFi OTA web server (see RAM note below)
- `Heltec_v3_repeater_wormhole_noota` — **wormhole only, no observer**, without OTA — for a node that's only ever the wormhole's local end, not also reporting to the public MQTT observer

**Board RAM note**: each active MQTT connection (observer, wormhole) is a full `WiFiClient`+`PubSubClient` pair, with its own buffer and socket allocated on the heap at runtime — this is fine on boards with PSRAM (e.g. Heltec V4, 2MB PSRAM) but leaves little headroom on ESP32-S3 boards without it (e.g. Heltec V3, 320KB internal SRAM only, and the static-RAM numbers reported at compile time barely reflect this runtime cost). Two things that shrink it further:
- `DISABLE_WIFI_OTA=1` drops `ESPAsyncWebServer`/`AsyncElegantOTA`/`AsyncTCP` entirely (~124KB flash saved on V3, negligible *static* RAM change since that library only allocates on the heap when `start ota` is actually invoked) — this removes a heap-spike risk if OTA were triggered while the wormhole is running, at the cost of losing the WiFi-based `start ota` command (still flashable via USB).
- Dropping `WITH_MQTT_BRIDGE` when a node doesn't need the public observer removes one whole `WiFiClient`+`PubSubClient` pair — modest at the static level (~1.5KB RAM / ~12KB flash on V3) but removes an entire concurrent TCP/MQTT connection's runtime heap footprint.

## 🖥 Screen Timeout Toggle (Repeater/Room Server, custom addition)

The OLED screen auto-off (20s after last button press / boot) can now be toggled at runtime instead of only at compile time.

**New CLI command** (same console as other admin commands):

| Command | Effect |
|---|---|
| `set screen.timeout on\|off` | Enables/disables the 20s screen auto-off, persisted |
| `get screen.timeout` | Reads back the current setting |

Default depends on the build: builds with `WITH_MQTT_BRIDGE` and/or `WITH_MQTT_WORMHOLE_BRIDGE` (e.g. `heltec_v4_repeater_mqtt`, `heltec_v4_room_server_mqtt`, `Heltec_v3_repeater_mqtt`, `Heltec_v3_repeater_wormhole_noota`) default to `off` — the screen stays on, matching this fork's pre-existing behavior on those builds, since they're typically desk/bench devices where the extra status rows are worth keeping visible. Other builds default to `on`, matching the original always-timeout behavior. Either way, it's now changeable without reflashing.

## 💬 BBS testuale (Room Server, aggiunta custom)

Una BBS a comandi testuali per Heltec V4, costruita sopra `simple_room_server`: gli utenti si registrano con la propria chiave pubblica MeshCore (nessuna password condivisa), scrivono e leggono in stanze pubbliche, si scambiano mail private, e i moderatori/admin gestiscono ruoli, ban/mute e apertura delle stanze — tutto via messaggi diretti, senza server esterni. Note di progetto e di manutenzione complete in [UPSTREAM.md](./UPSTREAM.md).

**Come funziona**

- Tutta la logica vive in `lib/bbs/` (pura, senza dipendenze da Arduino/MeshCore, testata nativamente con `pio test -e native`) e `lib/bbs_port/` (l'adattatore concreto: LittleFS, orologio, invio messaggi). Il firmware vero e proprio è `examples/bbs_room_server/`, copia di `simple_room_server` con un solo punto di aggancio in `onPeerDataRecv`.
- I dati (utenti, post per stanza, mail, configurazione) sono su LittleFS in `/bbs/`, in una partizione dati da 8 MB dedicata (vedi l'environment `heltec_v4_bbs_room_server`), con log in append e integrità verificata via CRC16 — un log con la coda corrotta da uno spegnimento improvviso viene riparato all'avvio, non perso.
- Il primo utente che si registra sul nodo diventa automaticamente amministratore. Le notifiche di post nuovi sono asincrone e accorpate (es. "3 nuovi in Generale, N per leggere"), consegnate agganciandosi al traffico che la BBS riceve comunque — nessun timer dedicato.
- Le stanze iniziali (Generale, Annunci, Tecnico) e i limiti (dimensione messaggi, tasso anti-abuso, ritenzione post) sono costanti di compilazione in `lib/bbs/bbs_config.h`.

**Comandi** (un messaggio diretto al nodo, in inglese; le risposte sono in italiano)

| Comando | Effetto |
|---|---|
| `REGISTER <nome>` | Registrazione con la propria chiave pubblica |
| `LOGIN` | Bentornato, con conteggio di post e mail non letti (o il messaggio del giorno, se impostato) |
| `LOGOUT` | Chiude la sessione |
| `H` | Elenco comandi (ridotto se non ancora registrati) |
| `K` | Elenco delle stanze, con i post da leggere e il totale della stanza tra parentesi, es. `0:Generale(2/12)` |
| `E [stanza] <testo>` | Pubblica un post (stanza di default se omessa) |
| `N [stanza]` | Legge il prossimo messaggio non letto nella stanza |
| `S <stanza>` / `U <stanza>` | Iscriviti / disiscriviti dalle notifiche di una stanza |
| `M` | Legge la prossima mail privata non letta |
| `M <nome> <testo>` | Invia una mail privata |
| `SEARCH <stanza> <parola>` | Cerca una parola tra gli ultimi post della stanza |
| `WHO` | Chi è online adesso |
| `STATS` | Statistiche del nodo (utenti, post, mail) |
| `MOTD` | Mostra il messaggio del giorno |

**Comandi riservati** (moderatore o amministratore)

| Comando | Effetto |
|---|---|
| `BAN <nome>` / `UNBAN <nome>` | Un utente bannato non riceve più risposte né notifiche |
| `MUTE <nome>` / `UNMUTE <nome>` | Un utente silenziato può leggere ma non pubblicare |
| `DELPOST <stanza>` | Cancella l'ultimo post della stanza |
| `CLOSE <stanza>` / `OPEN <stanza>` | Una stanza chiusa resta leggibile ma rifiuta nuovi post |
| `MODLOG` | Conteggio delle azioni di moderazione registrate |
| `SETMOD` / `SETADMIN` / `SETUSER <nome>` | Cambia ruolo (solo amministratore) |
| `MOTD <testo>` / `MOTD CLEAR` | Imposta/rimuove il messaggio del giorno (solo amministratore) |

**Ambienti PlatformIO**

- `heltec_v4_bbs_room_server` — BBS pura, nessun bridge
- `heltec_v4_bbs_room_server_mqtt` — BBS + bridge/wormhole MQTT (vedi sopra)

Entrambi usano LittleFS (non SPIFFS) e una partition table dedicata (`variants/heltec_v4_bbs/partitions_bbs.csv`): flashare uno di questi environment su un nodo che aveva in precedenza un altro firmware **riformatta la partizione dati**, perdendo l'identità del nodo e ogni dato precedente — fare un backup completo della flash (`esptool read_flash`) prima di passare a questo firmware se si vuole poter tornare indietro.

**Non ancora implementato**: pannello di configurazione via CLI seriale (i parametri restano costanti di compilazione), statistiche di uptime/spazio libero/batteria e loro visualizzazione sull'OLED, avvisi automatici da sensori collegati al nodo.

## 🛠 Hardware Compatibility

MeshCore is designed for devices listed in the [MeshCore Flasher](https://meshcore.io/flasher)

## 📜 License

MeshCore is open-source software released under the MIT License. You are free to use, modify, and distribute it for personal and commercial projects.

## Contributing

Please submit PR's using 'dev' as the base branch!
For minor changes just submit your PR and we'll try to review it, but for anything more 'impactful' please open an Issue first and start a discussion. It is better to sound out what it is you want to achieve first, and try to come to a consensus on what the best approach is, especially when it impacts the structure or architecture of this codebase.

Here are some general principles you should try to adhere to:
* Keep it simple. Please, don't think like a high-level lang programmer. Think embedded, and keep code concise, without any unnecessary layers.
* No dynamic memory allocation, except during setup/begin functions.
* Use the same brace and indenting style that's in the core source modules. (A .clang-format is probably going to be added soon, but please do NOT retroactively re-format existing code. This just creates unnecessary diffs that make finding problems harder)

Help us prioritize! Please react with thumbs-up to issues/PRs you care about most. We look at reaction counts when planning work.

### Running unit tests

To run unit tests, run the following command:

```bash
pio test --environment native --verbose
```

## Road-Map / To-Do

There are a number of fairly major features in the pipeline, with no particular time-frames attached yet. In very rough chronological order:
- [X] Companion radio: UI redesign
- [X] Repeater + Room Server: add ACL's (like Sensor Node has)
- [X] Standardise Bridge mode for repeaters
- [ ] Repeater/Bridge: Standardise the Transport Codes for zoning/filtering
- [X] Core + Repeater: enhanced zero-hop neighbour discovery
- [ ] Core: round-trip manual path support
- [ ] Companion + Apps: support for multiple sub-meshes (and 'off-grid' client repeat mode)
- [ ] Core + Apps: support for LZW message compression
- [ ] Core: dynamic CR (Coding Rate) for weak vs strong hops
- [ ] Core: new framework for hosting multiple virtual nodes on one physical device
- [ ] V2 protocol spec: discussion and consensus around V2 packet protocol, including path hashes, new encryption specs, etc

## 📞 Get Support

- Report bugs and request features on the [GitHub Issues](https://github.com/ripplebiz/MeshCore/issues) page.
- Find additional guides and components on [my site](https://buymeacoffee.com/ripplebiz).
- Join [MeshCore Discord](https://meshcore.gg) to chat with the developers and get help from the community.
