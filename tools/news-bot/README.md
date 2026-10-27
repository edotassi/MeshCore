# news-bot

Servizio che legge feed RSS/Atom e pubblica i nuovi titoli su un broker MQTT (Mosquitto).
Il nodo BBS MeshCore (ESP32) si sottoscrive al topic e pubblica le notizie in una stanza del BBS.

```
feed RSS/Atom --> news-bot --(MQTT, QoS 1, firmato HMAC)--> Mosquitto --> BBS (ESP32) --> stanza "News"
```

Il traffico MQTT e' in chiaro (il client dell'ESP32 non ha TLS), quindi ogni messaggio e'
firmato con HMAC-SHA256: il BBS scarta qualunque messaggio non firmato o contraffatto.

## Formato del messaggio (v1)

Payload UTF-8, 6 righe separate da `\n`, senza `\n` finale:

```
v1
<id>        1..32 caratteri [0-9a-zA-Z_-]; qui: primi 16 hex di sha256(nome_feed + "\n" + guid/link)
<ts>        epoch unix (secondi) al momento della pubblicazione
<source>    etichetta della fonte, 1..16 byte (es. ANSA)
<title>     titolo, 1..140 byte UTF-8, su una riga
<hmac>      64 caratteri hex minuscoli = HMAC-SHA256(chiave, "v1\n<id>\n<ts>\n<source>\n<title>")
```

Pubblicazione con QoS 1, `retain=false`, sul topic `NEWS_TOPIC` (default `bbs/news`).

## Comportamento

- **Primo avvio di un feed**: tutte le voci attuali sono segnate come viste *senza* pubblicarle
  (niente raffica di vecchi titoli).
- **Limiti**: al massimo `max_per_run` voci nuove per feed (da `feeds.yaml`, default 2) e
  `MAX_PER_RUN` in totale per esecuzione (default 5). Le voci in eccesso **non** restano in
  coda: vengono segnate come viste e scartate, perche' le notizie invecchiano in fretta.
  Si pubblicano le piu' recenti, in ordine dalla piu' vecchia alla piu' nuova.
- **Filtri**: `include` / `exclude` (regex sul titolo, case-insensitive) per ogni feed. I titoli
  filtrati vengono segnati come visti.
- **Ore silenziose** (`QUIET_HOURS`, es. `23-7`, fuso `TZ`): in quel periodo non si pubblica nulla;
  le voci trovate restano in attesa e vengono pubblicate dopo, rispettando i limiti. Qualunque voce
  piu' vecchia di 6 ore (data di pubblicazione, o primo avvistamento se il feed non ha date) viene
  segnata come vista e scartata, anche fuori dalle ore silenziose.
- Una voce e' segnata come vista **solo dopo** la conferma (PUBACK) del broker. Se il broker non
  conferma, l'esecuzione si ferma e si riprova al ciclo successivo.
- Un feed che non risponde viene registrato nel log e saltato, gli altri proseguono.
- Titoli: tag HTML rimossi, entita' decodificate, spazi e a-capo compattati; se superano 140 byte
  sono troncati a fine parola con `…` (che conta nel budget), senza mai spezzare un carattere UTF-8.
- Stato: SQLite in `/data/state.db` (volume `news-data`).

## Deploy su Coolify

1. Coolify: **New Resource -> Docker Compose** dal repository git, branch del progetto,
   **Base Directory** `tools/news-bot`, file `docker-compose.yml`.
2. Imposta le variabili d'ambiente (vedi `.env.example`); quelle con `${VAR:?}` sono obbligatorie:

   | Variabile | Descrizione |
   |---|---|
   | `MQTT_PUBLISHER_USER` / `MQTT_PUBLISHER_PASSWORD` | credenziali del bot (solo scrittura sul topic) |
   | `MQTT_BBS_USER` / `MQTT_BBS_PASSWORD` | credenziali del BBS (solo lettura sul topic) |
   | `NEWS_HMAC_KEY` | chiave HMAC, base64, 32 byte |
   | `NEWS_TOPIC` | topic MQTT, default `bbs/news` |
   | `POLL_MINUTES` | intervallo di lettura, default 15 |
   | `MAX_PER_RUN` | massimo notizie per esecuzione, default 5 |
   | `QUIET_HOURS` | ore silenziose, es. `23-7` (vuoto = mai) |
   | `TZ` | fuso orario, default `Europe/Rome` |

   Genera la chiave con `./gen-key.sh` oppure:
   `python3 -c "import os,base64;print(base64.b64encode(os.urandom(32)).decode())"`
3. Apri la porta pubblica del broker (`MQTT_PUBLIC_PORT`, default **1884/tcp**) nel firewall della VPS (e nel firewall del provider, se presente).
   Per maggiore sicurezza limitala all'IP pubblico da cui si collega il BBS, se e' stabile.
4. Deploy. Mosquitto genera `passwd` e `acl` a ogni avvio dalle variabili d'ambiente; il bot parte
   dopo che Mosquitto e' healthy. Controlla i log di `news-bot`: al primo avvio vedrai
   "primo avvio ... segnate come viste".

Per cambiare i feed: modifica `feeds.yaml` nel repository e rifai il deploy, oppure decommenta
il volume `./feeds.yaml:/app/feeds.yaml:ro` in `docker-compose.yml`.

## Configurare il BBS

Via seriale (WiFi):

```
set wifi.ssid <nome-rete>
set wifi.pwd <password>
```

Via mesh, da amministratore:

```
ROOM ADD News
CLOSE <n>
NEWS SERVER <host-vps> 1884
NEWS USER <MQTT_BBS_USER> <MQTT_BBS_PASSWORD>
NEWS KEY <stesso NEWS_HMAC_KEY in base64>
NEWS ROOM <n>
NEWS ON
```

Il BBS deve usare una sessione MQTT persistente (clean session = false, client id fisso): cosi'
Mosquitto accoda (fino a 200 messaggi QoS 1) quelli pubblicati mentre e' offline.

## Aggiungere un feed

In `feeds.yaml`:

```yaml
  - name: Esempio          # univoco; non rinominare dopo il primo avvio
    url: https://example.org/rss.xml
    label: Esempio         # 1..16 byte, mostrato nel BBS
    max_per_run: 1
    include: "meshcore|lora"   # opzionale
    exclude: "oroscopo"        # opzionale
```

Il primo avvio del nuovo feed non pubblica nulla (vedi sopra). Il feed delle release di MeshCore
ha una voce per ogni tipo di firmware (Room Server, Repeater, Companion...): `include: "^Companion"`
ne tiene una sola per versione.

## Uso da riga di comando e test locali

```
python3 -m venv .venv && . .venv/bin/activate && pip install -r requirements-dev.txt
pytest

NEWS_HMAC_KEY=... python -m news_bot --once --dry-run --feeds feeds.yaml
```

- `--once`: un solo ciclo ed esce.
- `--dry-run`: stampa i messaggi (firmati) che verrebbero pubblicati, senza connettersi al broker e
  senza modificare lo stato (lavora su una copia in memoria; `STATE_PATH` per puntare allo stato
  reale, default `/data/state.db`). Per un feed nuovo mostra le ultime voci ma non le "pubblica".
  Senza `NEWS_HMAC_KEY` usa una chiave nulla (firme non valide).

## Sicurezza

- MQTT e' **in chiaro**: chi intercetta il traffico puo' leggere i titoli e le password MQTT.
  L'HMAC garantisce solo autenticita'/integrita' dei messaggi, non la riservatezza.
- Usa password lunghe e casuali, diverse per publisher e BBS, e una chiave HMAC generata a caso.
- Le credenziali del publisher non vanno mai sul BBS: il BBS ha un utente di sola lettura
  (ACL), quindi anche se il dispositivo viene compromesso non puo' pubblicare sul topic.
- Anche se la password MQTT del BBS venisse scoperta, senza `NEWS_HMAC_KEY` non si possono
  forgiare messaggi accettati. Il messaggio contiene `ts` e `id` ma il BBS decide se accettare
  duplicati o messaggi vecchi (replay).
- Limita la porta pubblica del broker con il firewall quando possibile. Non committare mai il file `.env`.
