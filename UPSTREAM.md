# BBS — note di manutenzione e merge

## Modello adottato

Questo repository è un fork personale (`origin` = edotassi/MeshCore) senza un
remote `upstream` separato: il ramo `bbs` parte direttamente da `main` e vi
confluiscono anche gli aggiornamenti futuri di MeshCore quando `main` viene
aggiornato. Non esiste un ramo "del fork" distinto da `main` da tenere
sincronizzato: il modello a più rami descritto nel piano originale si riduce
quindi a `main` (base) → `bbs` (lavoro) → merge periodici da `main` a `bbs`.

## Punto di partenza

- Commit di `main` da cui `bbs` è stato creato: `49e8cf0f86fca86ca8f9407ccb0b54caee8f09bd`
  ("feat: allow wormhole bridge to run without MQTT observer").
- `examples/bbs_room_server/` è una copia di `examples/simple_room_server/`
  a questo stesso commit. Per vedere cosa upstream ha cambiato nel Room
  Server dopo questo punto: `git diff 49e8cf0f -- examples/simple_room_server`.

## Decisioni tecniche prese in Fase 0

- **Filesystem: LittleFS**, non SPIFFS. `FILESYSTEM` è tipizzato come `fs::FS`
  in `src/helpers/IdentityStore.h`, quindi è compatibile con entrambi; lo
  strato di adattamento della BBS dovrà includere `<LittleFS.h>` al posto di
  `<SPIFFS.h>` e chiamare `LittleFS.begin(...)`. Motivo: resilienza allo
  spegnimento improvviso e adeguatezza a un log in append, entrambi requisiti
  espliciti della Fase 1 (storage messaggi) e dei test di persistenza.
- **Partition table custom**: `variants/heltec_v4_bbs/partitions_bbs.csv`,
  16 MB totali suddivisi in:
  - `app0` / `app1`: 0x3F0000 (≈3,94 MB) ciascuno, per OTA a due slot.
  - `spiffs` (nome di partizione richiesto da `LittleFS.begin()`, che di
    default cerca una partizione con questa label; il contenuto è comunque
    LittleFS): 0x800000 (8 MB), per i dati della BBS.
  - `nvs`, `otadata`, `coredump`: invariati rispetto a `default_16MB.csv`
    upstream.
  - Capacità stimata: ~44.000 messaggi nel caso peggiore (post da 160 byte),
    ~85.000 nel caso tipico (post più corti), condivisi tra tutte le stanze
    e la mail privata. La ritenzione configurabile (Fase 4) sovrascrive i
    messaggi più vecchi quando lo spazio si esaurisce.
- Nessuna riga aggiunta al `platformio.ini` di root: il glob
  `extra_configs = variants/*/platformio.ini` raccoglie già
  `variants/heltec_v4_bbs/platformio.ini` automaticamente.

## Varianti disponibili

- `heltec_v4_bbs_room_server`: BBS pura, nessun bridge MQTT/wormhole.
- `heltec_v4_bbs_room_server_mqtt`: BBS + `WITH_MQTT_BRIDGE` + `WITH_MQTT_WORMHOLE_BRIDGE`,
  stesso pattern degli environment `heltec_v4_room_server` / `heltec_v4_room_server_mqtt`
  già presenti sul fork. Estende `env:heltec_v4_bbs_room_server` (nota la sintassi
  `env:` richiesta da PlatformIO quando si estende un `[env:...]` invece di una
  sezione base), quindi eredita automaticamente LittleFS e la partition table.

## File upstream toccati

- `src/helpers/ConfigSerializer.cpp`: aggiunto `#include <cstdlib>` (una riga).
  Motivo: senza questo include, `atoi`/`atol`/`atof` non sono dichiarati sui
  toolchain host più recenti (riscontrato con Apple clang 21 su questa
  macchina) e `pio test -e native` non compila affatto — bug preesistente,
  non causato dalla BBS, ma bloccava la verifica di *tutti* i test nativi
  (compreso quello già esistente `test_config_serializer`). Fix minimo,
  nessun cambio di comportamento, sicuro per il firmware reale (verificato
  che `heltec_v4_bbs_room_server` compila ancora identico dopo il fix).

Per il resto, nessuno. Tutto il lavoro vive in file nuovi:
- `examples/bbs_room_server/` (copia di `examples/simple_room_server/`)
- `variants/heltec_v4_bbs/` (nuovo environment + partition table)
- `lib/bbs/` (nucleo BBS puro, Fase 1 — vedi sotto)
- `lib/bbs/port/` (adattatori MeshCore concreti, non ancora creato — arriva
  col prossimo blocco della Fase 1, insieme al wiring in
  `examples/bbs_room_server/MyMesh.cpp`)
- `test/test_bbs_*/`, `test/mocks/bbs_*` (test nativi GoogleTest)

## Verifica Fase 0

- `pio run -e heltec_v4_bbs_room_server` compila con successo
  (Flash: 1.164.693 / 4.128.768 byte — 28,2%; RAM: 63.924 / 2.097.152 byte — 3,0%).
- Prova su hardware V4 e flash reale: eseguita (backup completo della flash
  precedente via `esptool read_flash`, poi flash di `heltec_v4_bbs_room_server`).

## Fase 1 — primo incremento: nucleo identità (REGISTER/LOGIN/LOGOUT/H, benvenuto one-shot)

Vedi il piano completo (decisioni, formati record, macchina a stati) in
`.claude/plans/imperative-yawning-bumblebee.md` sulla history di questa
sessione. Riassunto:

- `lib/bbs/bbs_config.h`, `bbs_types.h`, `bbs_result.h`: costanti e POD
  condivisi. `BBS_MAX_ROOMS=8`, `BBS_NICK_LEN=16`, `BBS_USER_RECORD_SIZE=104`,
  `BBS_MAX_USERS=500`, `BBS_MAX_TEXT_LEN=151` (160 byte convenzione codebase
  meno 9 byte di framing applicativo, vedi `pushPostToClient` in
  `examples/bbs_room_server/MyMesh.cpp`).
- `lib/bbs/bbs_port.h`: interfacce pure `IClock`/`IFile`/`IFileSystem`
  (+ `ClientRef`/`IReplyChannel`, riservate per Fase 2+). Zero dipendenze
  Arduino/MeshCore — le implementazioni concrete arrivano nel prossimo blocco.
- `lib/bbs/bbs_user_store.*`: tabella utenti (pubkey → nickname/ruolo/flag),
  record fissi a 104 byte, accesso diretto per id via `seek`, lookup per
  pubkey via scansione lineare. **Deliberatamente separata da
  `src/helpers/ClientACL.h`** (condiviso con repeater/room_server upstream):
  nessuna modifica a quel file. `touchLogin` aggiorna un record esistente
  riscrivendo l'intero file su un temporaneo e rinominandolo sopra
  l'originale (nessuna apertura contemporanea in lettura+scrittura).
- `lib/bbs/bbs_pending_welcome.*`: file append-only di pubkey già accolte,
  per il messaggio di benvenuto "una tantum".
- `lib/bbs/bbs_command_parser.*`: dispatch REGISTER/LOGIN/LOGOUT/H, gate
  pre/post-registrazione. La chiave pubblica è la credenziale: niente
  password, niente vera sessione in questo incremento (arriva in Fase 2).
- `lib/bbs/bbs_strings_it.h`: testi italiani, **senza lettere accentate**
  (decisione presa qui: risparmio byte + niente rischi di resa su
  OLED/client — punto "aperto" del piano originale, ora chiuso).
- `lib/bbs/bbs_tokenize.*`: tokenizer puro (non si può riusare
  `mesh::Utils::parseTextParts`, dipende da `mesh::Utils`).
- Test: `test/test_bbs_tokenize/`, `test/test_bbs_user_store/`,
  `test/test_bbs_pending_welcome/`, `test/test_bbs_command_parser/` (ognuno
  una directory propria — convenzione PlatformIO: ogni sottocartella diretta
  di `test/` prefissata `test_` è una suite a sé), con mock condivisi in
  `test/mocks/bbs_fake_filesystem.h` e `bbs_fixed_clock.h`. **37/37 test
  passano** con `pio test -e native` (nessuna modifica a `platformio.ini`:
  `lib/bbs/` viene raccolto automaticamente dal Library Dependency Finder).

**Nota locale, non di progetto**: su questa macchina `pio test -e native`
fallisce in fase di link con un errore SDK (`tapi error: malformed file`,
mismatch tra Xcode.app e i Command Line Tools standalone dopo l'update a
Darwin 27). Workaround non invasivo, solo per l'invocazione:
`SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk pio test -e native`.
Non è stato toccato `xcode-select` né altra configurazione di sistema.

**Fuori da questo incremento** (prossimo blocco): `lib/bbs/port/` concreto
(LittleFS, PSRAM, RTC), wiring in `examples/bbs_room_server/MyMesh.cpp`
(nuovo metodo `sendBbsReply`, sostituzione del ramo `TXT_TYPE_PLAIN` in
`onPeerDataRecv`, init in `MyMesh::begin`), bump `-D MAX_CLIENTS=500` nel
solo env BBS, storage post/stanze (K/N/E) e mail (M).

## Fase 1 — secondo incremento: stanze e post (K/N/E)

- `lib/bbs/bbs_post_store.*`: log append-only per stanza
  (`/bbs/r<NN>.log`, NN = id a 2 cifre), record a lunghezza variabile con
  intestazione fissa da 13 byte (version, room_id, timestamp, user_id,
  text_len, flags) + testo + CRC16-CCITT finale (record totale 15+text_len
  byte). `repairRoom()` ripara un log con coda troncata/CRC invalido
  (spegnimento durante una scrittura) riscrivendolo su un file temporaneo e
  rinominandolo sopra l'originale — va chiamato una volta per stanza
  all'avvio, prima di qualunque `appendPost`. **Nessun indice PSRAM in
  questo incremento**: le query (`findNextUnread`) scandiscono il file
  direttamente dall'inizio, che e' esattamente il percorso di fallback che
  l'indice (quando verrà aggiunto) userà comunque se la PSRAM non è
  disponibile — quindi questo lavoro non si getta via, resta il fallback.
- `lib/bbs/bbs_room_registry.h`: elenco statico delle stanze attive
  (`Generale`/`Annunci`/`Tecnico`, id 0/1/2), `constexpr`, nessuna
  allocazione. La configurazione a runtime (Fase 3) potrà sostituirlo senza
  cambiare l'interfaccia (`findRoom`).
- `bbs_user_store`: aggiunti `getLastRead`/`setLastRead` (puntatore
  "ultimo letto" per utente e stanza), tramite lo stesso schema di
  riscrittura-su-temporaneo già usato da `touchLogin` (fattorizzato in
  `rewriteApplying`).
- Comandi aggiunti al parser (solo per utenti registrati — un utente non
  registrato che digita K/N/E riceve ancora il benvenuto/messaggio
  "comando sconosciuto", scelta minimale per questo incremento, rivedibile
  se si vuole lettura pubblica per gli ospiti):
  - `K`: elenco stanze con id (es. "0:Generale 1:Annunci 2:Tecnico").
  - `E [stanza] <testo>`: pubblica un post; stanza di default 0 se omessa.
  - `N [stanza]`: legge il prossimo messaggio non letto nella stanza
    (default 0), avanzando il puntatore "ultimo letto" dell'utente.
  - **Sintassi dello stanza-id**: un token puramente numerico isolato a
    inizio comando è *sempre* interpretato come id di stanza (valido o
    meno); solo testo che non inizia con un numero isolato è trattato come
    contenuto libero. Limite noto e documentato nel codice: un post non può
    iniziare con un numero isolato seguito da spazio (va riformulato, es.
    "oggi ci sono 42 gradi" invece di "42 gradi oggi").
  - Niente paginazione "+N altri" nella risposta di `N` in questo
    incremento (si rilegge chiamando N più volte finché non risponde
    "nessun nuovo messaggio"): scelta per restare nel budget di 151 byte,
    aggiungibile in seguito senza cambiare il formato dei record.
- Test: nuova suite `test/test_bbs_post_store/` (append/lettura in ordine,
  stanze indipendenti, testo vuoto/troppo lungo rifiutato, id stanza non
  valido rifiutato, riparazione di una coda corrotta simulando uno
  spegnimento a metà scrittura). `test/test_bbs_command_parser/` esteso con
  K/N/E (stanza di default, stanza esplicita, id non valido, testo vuoto).
  **80/80 test passano** con `pio test -e native`.
- Bug trovato e corretto durante lo sviluppo: la prima versione del parsing
  dell'id-stanza rendeva il messaggio "stanza non valida" codice
  irraggiungibile (un numero fuori range veniva silenziosamente trattato
  come testo del post nella stanza di default, invece di segnalare
  l'errore) — risolto distinguendo esplicitamente "nessun prefisso
  numerico" da "prefisso numerico ma stanza inesistente".

## Fase 1 — terzo incremento: mail privata (M)

- Estratti in comune (prima duplicati tra `bbs_user_store` e
  `bbs_post_store`) `lib/bbs/bbs_binary_io.h` (lettura/scrittura interi
  little-endian su buffer grezzi) e `lib/bbs/bbs_crc16.h/.cpp` (CRC16-CCITT):
  la mail introduce un terzo formato record che avrebbe altrimenti
  triplicato la stessa logica di bit-fiddling, con rischio concreto di
  divergenza silenziosa tra i tre formati.
- `lib/bbs/bbs_mail_store.*`: un unico log condiviso `/bbs/mail.log` (non
  un file per destinatario: con al più poche centinaia di utenti e un
  volume di mail modesto per una comunità mesh, uno scan lineare filtrato
  per destinatario è più semplice che gestire centinaia di file), stesso
  formato/robustezza (CRC16, `repair()`) del log dei post.
- `bbs_user_store`: aggiunti `findByNickname` (lookup inverso, serve per
  risolvere il destinatario di `M <nickname> ...`) e
  `getLastMailRead`/`setLastMailRead` (cursore di lettura mail, riusa il
  campo `last_mail_read_seq` già presente nel record utente fin dal primo
  incremento).
- `bbs_tokenize`: aggiunto `extractToken` (come `extractCommand` ma senza
  maiuscolizzare) — i nickname sono case-sensitive, `extractCommand` non
  era riusabile per estrarre il destinatario di una mail.
- Comando `M` (solo utenti registrati):
  - `M` da solo: legge la prossima mail non letta ("nessuna mail nuova" se
    non ce ne sono).
  - `M <nickname> <testo>`: invia; "utente non trovato" se il nickname non
    esiste, "testo vuoto" se manca il messaggio.
- Test: nuova suite `test/test_bbs_mail_store/` (consegna al destinatario
  giusto, ordine cronologico, testo vuoto/troppo lungo rifiutato,
  riparazione coda corrotta) e `test/test_bbs_command_parser/` esteso con
  invio/lettura, destinatario sconosciuto, testo vuoto, indipendenza dalle
  stanze. **92/92 test passano** con `pio test -e native`; firmware
  `heltec_v4_bbs_room_server` ricompila senza errori.

**Fuori da questo incremento** (resta il prossimo blocco, richiede
hardware): `lib/bbs/port/` concreto (LittleFS, PSRAM, RTC) e wiring in
`examples/bbs_room_server/MyMesh.cpp`.

## Fase 1 — quarto incremento: adattatore concreto e aggancio in MyMesh.cpp

Chiude il Nucleo: la BBS ora gira per davvero sul firmware, non solo nei
test nativi.

- **Libreria divisa in due**, scoperta durante lo sviluppo:
  `pio test` (nativo) compila *tutti* i `.cpp` di una libreria referenziata,
  incluse le sottocartelle — mettere l'adattatore concreto in
  `lib/bbs/port/` (come previsto dal design originale) faceva sì che i test
  nativi tentassero di compilare anche file con `#include <FS.h>`/
  `<MeshCore.h>`, inesistenti fuori da un build ESP32. Soluzione: **due
  librerie separate**, non una libreria con sottocartella:
  - `lib/bbs/` — invariata, logica pura, zero dipendenze Arduino/MeshCore,
    e' quella che `pio test -e native` scopre e compila automaticamente.
  - `lib/bbs_port/` (nuova, sostituisce `lib/bbs/port/`) — `bbs_port_adapter.h/.cpp`,
    `bbs_port_littlefs.h`, `bbs_port_clock.h`: unico punto che include
    `<FS.h>`/`<MeshCore.h>`. Referenziata SOLO dall'environment ESP32
    (`build_src_filter` + `-I lib/bbs_port`), mai da `[env:native]`.
- **`lib/bbs_port/bbs_port_littlefs.h`**: `IFileSystem`/`IFile` sopra
  `fs::FS`/`fs::File` (LittleFS su ESP32). Pool statico di 3 slot file
  (nessuna allocazione dopo il setup; il codice BBS non apre mai più di 2
  file insieme, es. sorgente+temporaneo durante una riparazione).
  `LittleFsFileSystem::open()` per il modo `'a'`/`'w'` passa `create=true`
  (pattern gia' usato da `openAppend()`/`ClientACL` in MyMesh.cpp).
- **`lib/bbs_port/bbs_port_clock.h`**: `IClock` sopra `mesh::RTCClock*`.
- **`lib/bbs_port/bbs_port_adapter.*`**: unico punto d'ingresso usato da
  MyMesh — `bbs::port::init(fs::FS&, mesh::RTCClock*)` (chiamato una volta
  da `MyMesh::begin`, crea `/bbs/` con `mkdir` — LittleFS, a differenza di
  SPIFFS, richiede che la directory esista prima di poter creare un file al
  suo interno — poi le istanze di `UserStore`/`PendingWelcomeTable`/
  `PostStore`/`MailStore` con **un solo `new` a testa, mai più**, stesso
  schema già in uso nel resto del firmware per gli oggetti costruiti una
  tantum all'avvio, es. `StaticPoolPacketManager`) e
  `bbs::port::handleClientMessage(pub_key, input, out, out_cap)`.
- **Modifiche a `examples/bbs_room_server/`** (file nostro, non condiviso —
  divergenza attesa e prevista fin dalla Fase 0):
  - `MyMesh.h`/`main.cpp`: `SPIFFS` → `LittleFS` (era rimasto un secondo
    riferimento a `SPIFFS.format()` in `formatFileSystem()`, dimenticato
    nel passaggio di Fase 0 — trovato solo ora dal linker, non da
    ispezione manuale).
  - `MyMesh.h`: incluso `bbs_port_adapter.h` (unico punto del firmware che
    tocca header della BBS).
  - `MyMesh::begin()`: aggiunta `bbs::port::init(*_fs, getRTCClock())` dopo
    `acl.load(...)`.
  - `onPeerDataRecv`, ramo `TXT_TYPE_PLAIN`: il gate `PERM_ACL_GUEST` +
    `addPost(...)` e' sostituito da `bbs::port::handleClientMessage(...)`.
    **Il filtro per ruolo ClientACL sparisce**: la BBS risponde a
    qualunque client arrivato fin qui, perché il concetto di ruolo BBS
    (registrato/moderatore/admin) vive in `UserStore`, indipendente dai
    permessi ClientACL. Il resto del ramo (calcolo ack, scelta
    `sendDirect`/`sendFloodReply`, gestione `is_retry`) resta invariato:
    la BBS riusa il meccanismo di invio/ack gia' collaudato.
  - Nuovo metodo pubblico `MyMesh::sendBbsReply(...)`: stesso pattern di
    `pushPostToClient` (createDatagram + sendDirect/sendFloodScoped). Non
    ancora sul percorso critico di nessun comando (la risposta sincrona
    riusa il meccanismo di ack esistente) — e' il seam per notifiche
    asincrone future (Fase 2+, es. avviso di mail in arrivo).
  - `addSystemPost`/`storePost`/`pushPostToClient`/CLI admin/ACL
    admin-room-guest in `onAnonDataRecv`: **intatti, nessuna modifica**.
- **`ROOM_PASSWORD` svuotata** (`-D ROOM_PASSWORD='""'`, era `"hello"`,
  ereditato invariato dal Room Server originale): la BBS deve essere
  raggiungibile senza una password condivisa in anticipo. Un client che si
  connette con password vuota (il caso comune quando non se ne conosce
  una) ottiene automaticamente `PERM_ACL_READ_WRITE` a livello ACL
  (`onAnonDataRecv`, verificato leggendo il codice: `strcmp("", guest_password)`
  con `guest_password` anch'essa vuota). L'autenticazione vera
  (REGISTER/LOGIN) resta un livello sopra, gestita dalla BBS stessa,
  indipendente da questo.
- **`-D MAX_CLIENTS=500`** (solo in questo environment, default upstream
  20 in `src/helpers/ClientACL.h`, non toccato): ogni utente registrato
  nella BBS deve anche poter restare un contatto ClientACL noto (serve il
  segreto condiviso per decifrare i suoi messaggi), e 20 e' pensato per un
  room server generico. Costo verificato: +78 KB circa di RAM statica
  (63.964 → 142.684 byte su 2.097.152, 6,8%), accettabile.
- Verifica: `pio run -e heltec_v4_bbs_room_server` e
  `_room_server_mqtt` compilano e linkano (Flash 1.180.181/4.128.768 —
  28,6%); **92/92 test nativi** ancora verdi dopo la separazione delle
  librerie. Prova su hardware reale (flash + REGISTER/LOGIN/H/K/N/E/M da
  un client vero): non ancora eseguita in questa sessione.

**Nucleo (Fase 1) completo**: identità (REGISTER/LOGIN/LOGOUT/H, benvenuto
one-shot), stanze e post (K/N/E), mail privata (M), adattatore concreto e
aggancio nel firmware. Restano fuori dal Nucleo, per le fasi successive del
piano originale: notifiche brevi di post nuovi (Fase 2), ruoli/moderazione/
anti-abuso (Fase 3), funzioni accessorie come MOTD/statistiche/ricerca
(Fase 4) — e, opzionale, l'indice dei post in PSRAM (oggi le query
scandiscono il file direttamente; funziona, e' solo un'ottimizzazione).

## Verifica su hardware reale (V4)

Flash di `heltec_v4_bbs_room_server` sul nodo V4, con un secondo dispositivo
MeshCore reale (telefono + companion radio) come client. Il log pacchetti
seriale (`log start` poi `log` via CLI ammin, sender_timestamp=0) mostra
scambi RX(TXT_MSG)→TX(ACK)→TX(TXT_MSG risposta)→RX(ACK) ripetuti, con
lunghezze di risposta diverse a comandi diversi (116, 52, 84, 52, 68, 84
byte di payload su 6 scambi) — coerente con REGISTER/H/altri comandi che
producono testi di lunghezza diversa, non sempre la stessa risposta.
REGISTER confermato funzionante dall'utente. Verifica completa
comando-per-comando (K/E/N/M/S/U) rimandata a una sessione successiva.

## Fase 2 — primo incremento: sessioni, iscrizioni persistenti, conteggio al login

Dal piano originale (Fase 2, "Notifiche brevi"), la parte pura e
testabile nativamente, senza toccare `examples/bbs_room_server/MyMesh.cpp`:

- **`lib/bbs/bbs_session_table.h/.cpp`** (nuovo): "chi e' online adesso",
  solo RAM, array statico da `BBS_MAX_SESSIONS=32` (indipendente da
  `BBS_MAX_USERS=500`: gli account registrati possono essere molti di piu'
  di quanti sono attivi in un dato momento). Stesso principio di eviction
  di `ClientACL` (sostituisce la voce meno recentemente attiva quando
  piena). `touch()` ad ogni comando di un utente registrato,
  `logout()` esplicito su `LOGOUT`, `isActive()` con timeout configurabile
  (`BBS_SESSION_TIMEOUT_SECS=2400`, 40 minuti — nel mezzo dei 30-60
  suggeriti dal piano).
- **Iscrizioni alle stanze, persistite** (non nella sessione, che e'
  volatile): riuso di 2 degli originari 10 byte `reserved` nel record
  utente (`subscribed_rooms`, bitmask) — **nessuna modifica alla
  dimensione del record** (resta 104 byte) ne' migrazione necessaria: i
  record esistenti hanno gia' quei byte a zero, letto correttamente come
  "nessuna iscrizione". Di default alla registrazione: Generale e Annunci
  (bit 0/1); Tecnico resta opt-in (`bbs_room_registry.h`,
  `kDefaultSubscribedMask`).
- Comandi `S <stanza>` / `U <stanza>` (iscriviti/disiscriviti,
  solo utenti registrati, id di stanza obbligatorio — sintassi diversa da
  E/N dove il prefisso numerico e' opzionale).
- `PostStore::countUnread` / `MailStore::countUnread` (nuovi, scan
  completo — pensati solo per il LOGIN, non sul percorso caldo).
- **`LOGIN` ora riporta i non letti**: somma i post non letti in tutte le
  stanze a cui l'utente e' iscritto piu' le mail non lette, invece del
  semplice "bentornato" fisso. `kLoginWelcomeBack` sostituita da
  `kLoginWelcomeBackFmt` (format string per `snprintf`, unico posto dove
  si compone testo con numeri — il resto del sistema resta puro
  prefisso/suffisso).
- Test: nuova suite `test/test_bbs_session_table/` (attivazione, scadenza,
  rinnovo, logout, eviction quando piena) e le esistenti estese
  (sottoscrizioni in `test_bbs_user_store`, `countUnread` in
  `test_bbs_post_store`/`test_bbs_mail_store`, conteggio al login e
  S/U in `test_bbs_command_parser`). **109/109 test nativi** verdi;
  entrambe le varianti ESP32 ricompilano (Flash 1.181.425/4.128.768 —
  28,6%; RAM 142.692/2.097.152 — 6,8%, invariata rispetto al blocco
  precedente: la tabella sessioni e' un array statico piccolo).

**Esplicitamente fuori da questo incremento**: la consegna vera e propria
delle notifiche push quando arriva un post nuovo ("1 nuovo in Generale, N
per leggere", inviata senza che l'utente debba scrivere nulla). Richiede:
un hook periodico da `MyMesh::loop()` (nuovo aggancio hardware, come il
blocco precedente), la capacita' di risolvere pubkey→`ClientInfo*` tramite
`ClientACL` dal port layer (oggi il port layer riceve solo la pubkey a
messaggio in arrivo, non un accesso permanente all'ACL), l'accorpamento
delle notifiche ravvicinate (5 post in 2 minuti = 1 avviso) e la gestione
dei mancati recapiti. E' un blocco a se', verificabile solo su hardware
come il precedente — il seam (`MyMesh::sendBbsReply`, `IReplyChannel`) e'
gia' pronto dal blocco precedente in attesa di essere usato.

## Fase 2 — secondo incremento (completamento): consegna delle notifiche push

Chiude la Fase 2: le notifiche di post nuovi vengono davvero inviate, non
solo accumulate.

- **Decisione chiave**: niente hook periodico su `MyMesh::loop()`. La
  verifica "ci sono notifiche pronte da spedire" (`Notifier::tick`) si
  aggancia invece a *ogni* messaggio in arrivo alla BBS (dentro
  `bbs::port::handleClientMessage`, prima di processare il comando).
  Dimezza la superficie di aggancio hardware rispetto a quanto previsto:
  niente nuovo punto in `MyMesh::loop()`, un solo posto tocca ancora
  `examples/bbs_room_server/MyMesh.cpp`. Costo: una notifica pronta puo'
  aspettare finche' *qualcuno* (chiunque) non scrive di nuovo alla BBS,
  invece di partire al secondo esatto in cui scade la finestra — accettabile
  per un sistema gia' asincrono via LoRa.
- **`lib/bbs/bbs_notifier.h/.cpp`** (nuovo, puro): `onNewPost` (chiamato da
  `handlePost` dopo un `E` riuscito) segna "in sospeso" ogni sessione
  attiva, raggiungibile e iscritta alla stanza (autore escluso);
  `tick` invia gli avvisi la cui finestra e' scaduta e azzera i contatori.
- **`SessionTable` esteso** con lo stato per l'accorpamento: conteggio e
  orario del primo post in sospeso per stanza, mancati recapiti consecutivi
  e flag di raggiungibilita' (offline dopo `BBS_MAX_MISSED_DELIVERIES=3`,
  torna online al prossimo `touch()`, cioe' al prossimo contatto).
- **Bug trovato e corretto**: `MyMesh::sendBbsReply`, scritto nel blocco
  precedente come "seam per uso futuro" mai esercitato, mandava il testo
  grezzo senza il framing timestamp+tipo che ogni client si aspetta da un
  TXT_MSG — sarebbe stato silenziosamente illeggibile lato client. Scoperto
  solo ora che il percorso e' davvero usato, non da ispezione manuale.
- **`MyMesh::getBbsClient`** (nuovo, minimo): incapsula
  `acl.getClient(pubkey, PUB_KEY_SIZE)`, serve a `MyMeshReplyChannel` per
  risolvere una pubkey a un `ClientInfo*` prima di poter chiamare
  `sendBbsReply`.
- **`lib/bbs_port/bbs_port_reply_channel.h`**: dichiara `MyMeshReplyChannel`
  (implementa `IReplyChannel`) con `MyMesh` solo forward-dichiarata, per
  evitare l'inclusione circolare con `MyMesh.h` (che include
  `bbs_port_adapter.h`). **La sua implementazione pero' vive in
  `examples/bbs_room_server/bbs_reply_channel.cpp`, non in
  `lib/bbs_port/`** — scoperto durante lo sviluppo che un file dentro
  `lib/bbs_port/` che include per intero `MyMesh.h` non eredita gli stessi
  percorsi di include che il progetto principale ha per i header del
  framework (es. `LittleFS.h`): la libreria non "vede" quei percorsi anche
  se gia' funzionano per i sorgenti dell'esempio. Spostare l'unico file che
  ha bisogno dell'inclusione completa dentro `examples/bbs_room_server/`
  (dove gia' compila) evita il problema senza altri `-I` da indovinare.
- `bbs::port::init` prende ora anche un `IReplyChannel*`; `MyMesh::begin()`
  costruisce un `MyMeshReplyChannel` `static` (nessuna `new`, vive per
  tutta la durata del programma) e lo passa.
- Test: nuova suite `test/test_bbs_notifier/` (accorpamento, esclusione
  dell'autore, utenti non iscritti, offline dopo mancati recapiti
  ripetuti), `test_bbs_session_table` esteso con lo stato di
  accorpamento/raggiungibilita', nuovo mock `test/mocks/bbs_fake_reply_channel.h`.
  **123/123 test nativi** passano; entrambe le varianti ESP32 ricompilano
  (Flash 1.182.965/4.128.768 — 28,7%; RAM invariata).

**Fase 2 completa.** Prossimo: Fase 3 (ruoli, moderazione, configurazione,
anti-abuso) e Fase 4 (funzioni leggere: MOTD, chi c'e', statistiche,
ricerca, ritenzione), entrambe nel territorio testabile senza hardware.

## Fase 3: ruoli, moderazione, anti-abuso

Tutta questa fase e' pura logica testata nativamente — nessuna modifica a
`examples/bbs_room_server/` in questo blocco.

- **Bootstrap admin**: il primo utente mai registrato in un dato archivio
  diventa automaticamente `ROLE_ADMIN` (altrimenti nessuno potrebbe mai
  promuovere il primo admin senza un canale esterno). Tutti i successivi
  sono `ROLE_USER` finche' un admin non li promuove con `SETMOD`/`SETADMIN`.
  `UserStore` guadagna `getRole`/`setRole`.
- **Moderazione, persistita nel record utente** (riusa i bit liberi di
  `UserRecord::flags`, nessun cambio di formato): `isBanned`/`setBanned`
  (bit0), `isMuted`/`setMuted` (bit1). Un utente **bannato** non riceve
  *nessuna* risposta a *nessun* comando (controllato prima di qualunque
  dispatch, anche prima di registrare l'attivita' di sessione) ne'
  notifiche (`Notifier` controlla `isBanned` sia in `onNewPost` che in
  `tick`, per coprire anche il caso "bannato dopo che un post era gia' in
  coda"). Un utente **silenziato** puo' ancora leggere (`N`/`M` in lettura)
  ma non pubblicare (`E`) ne' mandare mail (`M <nome> <testo>`).
- **`PostStore::deleteLastPost`**: i post restano immutabili (append-only),
  "cancellare" marca un bit in `PostRecord::flags` (gia' previsto fin dal
  primo formato) invece di rimuovere fisicamente il record;
  `findNextUnread`/`countUnread` ora saltano i record marcati. Cancella
  sempre e solo l'ultimo post della stanza (niente numerazione dei post da
  esporre agli utenti per indicarne uno specifico — semplificazione
  deliberata, sufficiente per il caso d'uso comune "modero subito dopo che
  è stato pubblicato").
- **`lib/bbs/bbs_room_state.h/.cpp`** (nuovo): apertura/chiusura per
  stanza, persistita in un file di `BBS_MAX_ROOMS` byte. Una stanza chiusa
  rifiuta `E` ma resta leggibile con `N`.
- **`lib/bbs/bbs_mod_log.h/.cpp`** (nuovo): registro delle azioni riservate
  (chi, cosa, su chi/quale stanza, quando). **Solo RAM, circolare, capacita'
  fissa (32) — si perde al riavvio**, semplificazione deliberata: le azioni
  che conta (ban/mute/ruoli) sono gia' persistite dove serve davvero
  (`UserStore`); questo e' un log diagnostico "chi ha fatto cosa in questa
  sessione", non l'unica fonte di verita'. Può diventare un log persistito
  in append (stesso schema di `bbs_post_store`) in futuro, senza cambiare
  l'interfaccia. Comando `MODLOG` (moderatore+) espone solo il conteggio
  totale, non il dettaglio — un visualizzatore completo del registro non
  era necessario per soddisfare il criterio "ogni azione riservata compare
  nel registro".
- **Anti-abuso**: `SessionTable::allowMessage` aggiunge una finestra
  scorrevole di 60 secondi (`BBS_MAX_MESSAGES_PER_MINUTE=6`) per utente,
  applicata a `E` e a `M` in invio (non alla lettura). Il limite alle
  sessioni contemporanee esisteva gia' dalla Fase 2 (`BBS_MAX_SESSIONS`
  con eviction) — nessun lavoro aggiuntivo necessario, il criterio era
  gia' soddisfatto. La dimensione massima del post resta quella gia'
  imposta dal formato dei pacchetti (`BBS_MAX_TEXT_LEN`) — non e' stato
  aggiunto un limite piu' stretto configurabile a parte, per non introdurre
  un sistema di configurazione runtime parallelo (vedi nota sotto).
- **Comandi aggiunti** (con controllo di ruolo: moderatore+ per
  ban/unban/mute/unmute/delpost/close/open/modlog, admin per
  setmod/setadmin/setuser): `BAN`/`UNBAN <nome>`, `MUTE`/`UNMUTE <nome>`,
  `DELPOST <stanza>`, `CLOSE`/`OPEN <stanza>`, `SETMOD`/`SETADMIN`/`SETUSER
  <nome>`, `MODLOG`. Non elencati nell'help generico (`H`) — comandi
  avanzati, un moderatore/admin sa gia' che esistono, pattern comune in
  BBS/IRC.
- **Nota sulla "Configurazione" del piano originale**: il blocco previsto
  ("nome della BBS, elenco stanze, ritenzione, timeout, limiti, tramite CLI
  seriale e strumento di configurazione già usato per il Room Server") non
  e' stato implementato come sistema runtime separato in questo passaggio.
  I parametri restano costanti di compilazione in `lib/bbs/bbs_config.h`
  (gia' tutte in un solo posto, gia' facili da cambiare senza toccare la
  logica). Costruire un vero pannello di configurazione runtime (persistito,
  esposto via CLI seriale admin) e' un lavoro a se', con superficie
  hardware propria (si aggancerebbe a `CommonCLI`/`MyMesh::handleCommand`,
  il canale admin gia' esistente) — rimandato a un blocco dedicato futuro
  se servirà davvero modificare questi parametri senza ricompilare.
- Test: `test_bbs_user_store` esteso (ruoli, ban, mute — persistenza e
  indipendenza dei bit), `test_bbs_post_store` esteso
  (`deleteLastPost` e salto dei cancellati), nuove suite
  `test/test_bbs_room_state/` e `test/test_bbs_mod_log/`,
  `test_bbs_session_table` esteso (rate limit), `test_bbs_notifier` esteso
  (bannato mai notificato), `test_bbs_command_parser` esteso con l'intera
  matrice di permessi (admin/moderatore/utente semplice, bannato,
  silenziato, stanza chiusa, rate limit). **155/155 test nativi**
  passano; entrambe le varianti ESP32 ricompilano senza errori (Flash
  1.186.725/4.128.768 — 28,7%; RAM invariata, tutte le nuove strutture
  sono piccole).

**Fase 3 sostanzialmente completa** (con le due semplificazioni deliberate
documentate sopra: registro di moderazione solo RAM, niente pannello di
configurazione runtime separato). Prossimo: Fase 4 (funzioni leggere).

## Fase 4: funzioni leggere

Ultima fase del piano originale. Cinque delle sei funzioni previste sono
pura logica, testata nativamente; la sesta (avvisi da sensori) e' esclusa
esplicitamente — richiede un sensore fisico specifico non disponibile in
questa sessione, e "interfacciarsi con il sensore scelto" non e' un
compito che si possa fare in astratto.

- **Bug trovato durante lo sviluppo, non banale**: `REGISTER` non
  registrava mai attivita' di sessione (il `touch()` viveva solo nel ramo
  "utente gia' registrato" di `processCommand`, mai in quello di prima
  registrazione). Un utente appena iscritto non risultava "online" finche'
  non mandava un secondo comando qualsiasi — invisibile a `WHO`/`STATS`
  subito dopo essersi registrato. Scoperto scrivendo i test di `WHO` per
  questa fase, non da ispezione manuale. Corretto aggiungendo
  `ctx.sessions.touch(new_id, ...)` al ramo `RegisterResult::OK`.
- **Scoperta di design, non un bug**: chiedere `WHO` e' esso stesso un
  comando, quindi riattiva la sessione di chi lo chiede (stesso principio
  per cui *qualunque* comando rinnova l'attivita' — coerente, non
  un'eccezione). Significa che un utente vede sempre se stesso in `WHO`
  subito dopo averlo chiesto, anche se era scaduto un attimo prima. Il
  messaggio "nessuno online" (`kWhoNoOne`) resta come ramo difensivo, ma
  non è raggiungibile tramite l'interfaccia a comandi cosi' com'e' oggi.
- **Ritenzione** (`PostStore::enforceRetention`, `BBS_MAX_POSTS_PER_ROOM=2000`):
  a differenza di `deleteLastPost` (che nasconde con un bit), qui i post
  piu' vecchi vengono scartati per sempre quando una stanza supera il
  limite. Girato automaticamente dopo ogni `E` riuscito (non e' un comando
  a parte): uno scan completo della stanza ad ogni post e' stato valutato
  accettabile per il volume di traffico tipico di una mesh LoRa (molto piu'
  lento del tempo di lettura da flash anche per migliaia di record); se in
  futuro una stanza si avvicinasse davvero a quel volume con alta frequenza,
  si puo' aggiungere un contatore in RAM per non ricontare ad ogni post,
  senza cambiare l'interfaccia pubblica. **Scope deliberatamente limitato
  ai post** (come dice il piano originale, "per stanza"): non estesa alla
  mail, che e' un unico log condiviso tra tutti i destinatari — scartare le
  piu' vecchie lì cancellerebbe indiscriminatamente anche mail non lette di
  un utente poco chiacchierone per far posto a uno piu' attivo.
- **`lib/bbs/bbs_motd.h/.cpp`** (nuovo): messaggio del giorno, un file di
  testo grezzo (nessuna intestazione). Comando `MOTD`: senza argomenti lo
  mostra (chiunque puo', e' innocuo); con testo o `CLEAR` lo
  imposta/rimuove (richiede admin). **Quando impostato, sostituisce il
  conteggio dei non letti nella risposta di `LOGIN`** invece di
  affiancarlo — scelta deliberata per restare dentro il budget di 151 byte
  senza dover troncare in modo imprevedibile la combinazione di due testi a
  lunghezza variabile; un bollettino e' tipicamente piu' urgente di un
  conteggio, e i non letti restano comunque consultabili con `N`/`M`.
- **`WHO`**: elenca i nickname delle sessioni attive (lettura diretta della
  tabella sessioni, nessuna struttura nuova).
- **`STATS`**: utenti registrati totali, sessioni attive ora, post totali
  (somma su tutte le stanze), mail totali (`MailStore::totalCount`, nuovo —
  a differenza di `countUnread` non filtra per destinatario). *Non
  implementati* in questo comando, perche' richiedono la porta
  hardware (Fase successiva se servira'): uptime (serve `millis()`, non
  l'orologio epoch che la BBS gia' ha), spazio libero su flash
  (`LittleFS.totalBytes()/usedBytes()`), livello batteria. La visualizzazione
  sull'OLED del V4 menzionata dal piano originale e' anch'essa rimandata
  (tocca `UITask`, non la logica della BBS).
- **`SEARCH <stanza> <parola>`** (`PostStore::searchRecent`, nuovo): cerca
  all'indietro tra gli ultimi `BBS_SEARCH_MAX_SCAN=200` post della stanza
  (i piu' recenti) il piu' recente che contiene la parola come sottostringa
  esatta (case-sensitive, un solo token — niente ricerca per frase). Salta
  i post cancellati. "Indice minimo in PSRAM" del piano originale non
  implementato: uno scan diretto limitato a 200 record e' gia' il percorso
  di fallback che un futuro indice userebbe comunque quando la PSRAM non è
  disponibile, quindi questo lavoro resta valido se un indice verra'
  aggiunto in seguito.
- **Fuori scope, esplicitamente**: avvisi automatici da sensori (stanza
  dedicata per notifiche da un sensore collegato al nodo) — richiede un
  sensore reale e la sua integrazione specifica, non generalizzabile senza
  sapere quale hardware e' effettivamente collegato.
- Test: nuova suite `test/test_bbs_motd/`, `test_bbs_post_store` esteso
  (ritenzione, ricerca — inclusi i casi limite: finestra di scansione
  rispettata, post cancellati esclusi, stanze indipendenti), `test_bbs_mail_store`
  esteso (`totalCount`), `test_bbs_command_parser` esteso con `WHO`/`STATS`/`SEARCH`/`MOTD`
  e un test di integrazione end-to-end della ritenzione attraverso 2005
  comandi `E` reali (non solo a livello di `PostStore`). **177/177 test
  nativi** passano; entrambe le varianti ESP32 ricompilano (Flash
  1.189.109/4.128.768 — 28,8%; RAM invariata).

**Le quattro fasi del piano originale sono ora implementate** (Nucleo,
Notifiche brevi, Amministrazione, Utili e ancora leggeri), con le
semplificazioni deliberate elencate nei rispettivi paragrafi sopra e un
solo pezzo di lavoro hardware esplicitamente rimandato (statistiche
uptime/spazio libero/batteria e la loro visualizzazione sull'OLED). Non
ancora eseguita in questa sessione: una prova end-to-end su hardware reale
di tutti i comandi introdotti dopo il primo blocco della Fase 1
(REGISTER/LOGIN/H/K/E/N/M sono stati verificati su banco; S/U, le notifiche
push, e tutti i comandi di Fase 3/4 no).

## Rifiniture successive alla Fase 4

Emerse da domande dirette sul comportamento della BBS dopo il flash, non
dal piano originale:

- **`K` mostra anche i post da leggere, non solo il totale**: formato
  passato da `<id>:<nome>(<totale>)` a `<id>:<nome>(<da leggere>/<totale>)`,
  es. `0:Generale(2/12)`. Il totale (`countUnread(stanza, 0)`, tutti i post
  non cancellati) è uguale per chiunque chieda; i "da leggere" usano il
  puntatore "ultimo letto" di *chi* chiede, quindi due utenti diversi
  vedono lo stesso totale ma un "da leggere" diverso. `handleRooms` ora
  prende anche `uid` (prima solo `ctx`). Test aggiunto apposta:
  `RoomsUnreadDropsAfterReadingButTotalStaysTheSame`, che verifica che il
  totale non cambi leggendo, mentre il "da leggere" personale sì.
- Confermato per iscritto (non era ovvio dal codice a chi non lo scrive):
  `LOGIN` non accetta un nome utente — l'identità è sempre la chiave
  pubblica del pacchetto MeshCore che ha effettivamente spedito il
  comando, mai un dato scritto nel testo. Non è possibile impersonare un
  altro account senza la sua chiave privata.

Flash rifatto due volte durante queste rifiniture, stessa partition
table/environment di prima: nessuna riformattazione, dati preservati.

## Fase 5: stanze dinamiche

Chiude la parte di "Configurazione" rimandata in Fase 3 (vedi sopra): solo
l'elenco delle stanze, non i limiti numerici (dimensione messaggi, tasso
anti-abuso, ritenzione), che restano costanti di compilazione — nessuna
richiesta concreta di cambiarli senza riflashare, a differenza
dell'elenco stanze.

- **`lib/bbs/bbs_room_registry.h/.cpp`**: da array `constexpr` a classe
  `RoomRegistry`, persistita su file (`/bbs/room_registry.dat`) con lo
  stesso schema "lazy" di `bbs_room_state.h` (assente = i tre default
  storici: Generale, Annunci, Tecnico). Tenuta in RAM (al piu'
  `BBS_MAX_ROOMS` voci, poche decine di byte) con pattern
  "persisti-poi-applica": `addRoom`/`removeRoom` scrivono prima il file,
  e aggiornano lo stato in RAM solo se la scrittura riesce — niente
  rollback da gestire a mano. Il nuovo id assegnato da `addRoom` e'
  sempre il piu' basso libero in `[0, BBS_MAX_ROOMS)`: il bitmask di
  iscrizione e `last_read[]` nel record utente (`bbs_types.h`) sono
  indicizzati direttamente da `room_id`, non dalla posizione nell'elenco,
  quindi un id resta un identificatore stabile anche se altre stanze
  vengono aggiunte o cancellate.
- **Riassegnazione di un id cancellato**: dato che gli id sono una
  risorsa scarsa (`BBS_MAX_ROOMS = 8`), `removeRoom` li libera per il
  riuso — ma un id riassegnato a una stanza futura non deve eredire nulla
  della stanza precedente. `ROOM DEL` quindi, oltre a togliere la voce dal
  registro, chiama `PostStore::purgeRoom` (cancella `/bbs/rNN.log`),
  `UserStore::clearRoomForAllUsers` (azzera bit di iscrizione e
  `last_read` per quell'id, per *tutti* gli utenti — unica riscrittura
  dell'intero file utenti invece che di un solo record, come gia' faceva
  `rewriteApplying` per un id singolo) e `RoomState::setClosed(id, false)`
  (una stanza riassegnata parte sempre aperta). Scelta deliberata, chiesta
  esplicitamente: niente migrazione/archiviazione dei dati della stanza
  cancellata, si cancella e basta.
- **Comandi aggiunti** (solo amministratore, come `SETMOD`/`SETADMIN`):
  `ROOM ADD <nome>` (nome validato come i nickname: 1-15 caratteri,
  lettere/numeri/-/_, niente spazi; rifiutato se duplicato o se il
  registro è già a `BBS_MAX_ROOMS`) e `ROOM DEL <stanza>`. Non elencati in
  `H`, stesso criterio degli altri comandi riservati.
- **`kDefaultSubscribedMask`** resta una costante di compilazione (bit 0 e
  1, Generale e Annunci): è una scelta di prodotto su "a cosa si iscrive
  un utente nuovo", non una proprietà della singola stanza — una stanza
  aggiunta con `ROOM ADD` è sempre opt-in via `S`, come già lo era
  Tecnico.
- Tutti i punti che prima leggevano l'array globale `kRooms`/`kNumRooms`
  (`bbs_command_parser.cpp`, `bbs_notifier.cpp`, `bbs_port_adapter.cpp`)
  ora passano per `ctx.room_registry`/un riferimento a `RoomRegistry`
  iniettato nel costruttore (`Notifier` ne ha guadagnato uno).
  `CommandContext` guadagna il campo `room_registry`.
- Test: nuova suite `test/test_bbs_room_registry/` (default, add con id
  piu' basso libero, nomi duplicati/invalidi, registro pieno, remove e
  riuso dell'id), `test_bbs_command_parser` esteso con l'intera matrice
  `ROOM ADD`/`ROOM DEL` (permessi, errori, e la verifica end-to-end che un
  id riassegnato non eredita post/iscrizioni della stanza cancellata).
  **55/55** in `test_bbs_command_parser` (7 nuovi) e **6/6** nella nuova
  `test_bbs_room_registry` passano, `test_bbs_notifier` invariato a 8/8 —
  verificato compilando e linkando ogni unità di `lib/bbs/` (ambiente
  locale senza PlatformIO disponibile: verifica fatta con g++/gtest
  compilati da sorgente, non con `pio test -e native`); non ricompilato
  su hardware reale in questo passaggio.

## Fase 6: post fissati, export/import via seriale, liste multi-riga

- **Post fissati**: `kPostFlagPinned` nuovo bit in `PostRecord::flags`
  (`bbs_types.h`), al pari di `kPostFlagDeleted`. `PostStore` guadagna
  `pinLastPost`/`unpinRoom`/`findPinned`. Stessa convenzione di
  `deleteLastPost`: nessuna numerazione dei post esposta agli utenti,
  si opera sempre sull'ultimo post fisico della stanza. Al più un post
  fissato per stanza: `pinLastPost` pulisce il bit su tutti i record
  (riscrittura dell'intero file, come `deleteLastPost`/`enforceRetention`)
  prima di impostarlo sull'ultimo — fissarne uno nuovo sposta semplicemente
  il fissaggio, senza bisogno di un comando di sblocco esplicito.
  `findPinned` ignora un record marcato anche come cancellato (un
  `DELPOST` su un post fissato lo nasconde da `PINNED` senza bisogno di
  gestione incrociata nel comando). Comandi `PIN <stanza>`/`UNPIN <stanza>`
  (moderatore+, stesso schema di `CLOSE`/`OPEN`) e `PINNED [stanza]`
  (chiunque, prefisso stanza opzionale come `E`/`N`, stanza di default se
  omessa). `ModAction` guadagna `PIN_POST`/`UNPIN_POST`.
- **`export`/`import` via seriale**: comandi amministrativi che girano solo
  da console USB, mai dalla mesh LoRa — stesso meccanismo gia' usato da
  `get acl`/`erase` in `CommonCLI`/`MyMesh::handleCommand`
  (`examples/bbs_room_server/main.cpp` passa `sender_timestamp = 0` ai
  comandi letti da `Serial`; i branch in `MyMesh::handleCommand` controllano
  `sender_timestamp == 0` prima di eseguire, cosi' lo stesso testo ricevuto
  via radio verrebbe ignorato). Nuova `bbs::port::exportToSerial()`
  (`lib/bbs_port/bbs_port_adapter.h/.cpp`, unico punto oltre a
  `bbs_port_littlefs.h`/`bbs_port_clock.h` che tocca direttamente header
  Arduino) stampa su `Serial` un formato testuale versionato ("BBS EXPORT
  v1": sezioni `[ROOMS]`/`[USERS]`/`[POSTS room=N]`, campi separati da tab)
  pensato per essere riletto, utile come backup leggibile prima di un
  repair/riflash. **Deliberatamente esclude il contenuto della mail
  privata** (dato personale di due soli utenti, non necessario per un
  backup dei contenuti pubblici) e le iscrizioni/puntatori "ultimo letto"
  (stato poco interessante da conservare: dopo un ripristino tutti
  ripartono con i default e tutto da leggere) — se servisse in futuro,
  andrebbe dietro a una scelta esplicita documentata.
  `created_ts`/`last_login_ts` non sono preservati: dopo un import valgono
  il momento dell'import, non l'originale (semplificazione: nessuna logica
  della BBS dipende dal loro valore esatto).
  `bbs::port::importStart()`/`importInProgress()`/`importFeedLine()`
  implementano il ripristino, **solo su nodo vuoto** (rifiutato se
  `UserStore::count() > 0` — l'import non fa merge con dati esistenti, solo
  restore completo su un nodo appena flashato/cancellato, come richiesto).
  Il meccanismo di "modalita' incolla multi-riga" riusa un pattern gia'
  presente nello stesso file per un caso analogo:
  `region_load_active`/`StrHelper::isBlank` (caricamento bulk della region
  map via CLI, vedi `CommonCLI::startRegionsLoad`) intercetta ogni riga
  finche' non vede la riga di terminazione, esattamente come il mio
  `bbs::port::importInProgress()` controllato in cima a
  `MyMesh::handleCommand` intercetta ogni riga incollata finche' non vede
  `=== FINE EXPORT ===`. Dettagli del ripristino:
  - Utenti: pubkey (esadecimale, 32 byte) + nickname ricreano l'account via
    `registerUser` (che assegna id in ordine di registrazione, come
    all'origine); ruolo/ban/mute vengono poi riapplicati con
    `setRole`/`setBanned`/`setMuted` perche' `registerUser` forza sempre
    `ROLE_USER` tranne che per il primissimo utente (bootstrap admin) — che
    pero' coincide gia' con l'ammnistratore originale, visto che l'import
    ricrea gli utenti nello stesso ordine dell'export.
  - Stanze: un nodo vuoto parte gia' con Generale/Annunci/Tecnico
    (`RoomRegistry`); l'import aggiunge solo le stanze mancanti (l'id
    assegnato da `addRoom`, il piu' basso libero, torna a combaciare
    proprio perche' si parte da registro vuoto nello stesso ordine) e, cosa
    non ovvia, **rimuove le stanze di default che l'export non elenca**
    (`pruneRoomsNotSeenDuringImport`, stesso percorso di pulizia di `ROOM
    DEL`): se l'originale aveva cancellato Tecnico prima del backup, un
    ripristino naive l'avrebbe fatta ricomparire solo perche' e' un
    default del registro vuoto.
  - Post: un post fissato nell'originale viene ri-fissato subito dopo
    essere stato riscritto (`pinLastPost` sul post appena appeso, prima che
    altri lo seguano) invece di tentare di "spostare" un fissaggio dopo il
    fatto.
  - Errori di singola riga (pubkey non valida, autore non trovato, stanza
    che non riesce a riottenere lo stesso id) non interrompono l'import:
    vengono stampati su `Serial` e quella riga viene saltata, con un
    riepilogo "completato con errori" alla fine — scelta deliberata per non
    perdere il resto di un backup per un singolo record corrotto.
  - `examples/bbs_room_server/main.cpp`: il buffer `command` (dimensionato
    su `MAX_POST_TEXT_LEN`, sufficiente per un comando BBS via mesh) e'
    stato allargato (`BBS_SERIAL_LINE_LEN = MAX_POST_TEXT_LEN + 64`) perche'
    una riga `[POSTS]` incollata durante l'import (timestamp + autore +
    marcatore `PIN` + testo del post) puo' superare la sola lunghezza del
    testo — altrimenti sarebbe stata troncata prima di arrivare al parser.
- **Liste multi-riga**: `K` (elenco stanze) e `WHO` (elenco utenti online)
  ora separano le voci con `\n` invece che con uno spazio — richiesto
  esplicitamente per leggibilita' su OLED/client multi-riga. Verificato
  che non rompe nulla lato storage/framing: il testo dei post e' gia' un
  buffer length-prefixed (non delimitato, vedi `bbs_post_store.cpp`), il
  CRC16 tratta `\n` come un byte qualunque, e il payload TXT_MSG e' una
  stringa C senza filtri sui caratteri (`MyMesh::sendBbsReply`). Non
  verificato invece il rendering lato client/app companion (il repo non
  ne contiene il codice, solo il protocollo) — rischio di sola UX, non di
  correttezza: se il client non va a capo su `\n`, la stringa resta
  comunque leggibile, solo su una riga sola.
- Test: nuovi `test_bbs_post_store` (pin/unpin/findPinned, inclusa
  l'interazione con `deleteLastPost`) e nuovi casi in
  `test_bbs_command_parser` (permessi, sostituzione del fissaggio, lettura
  con/senza prefisso stanza); stringhe dei nuovi messaggi aggiunte al test
  di budget byte. **61/61** in `test_bbs_command_parser` (6 nuovi) e
  **24/24** in `test_bbs_post_store` (6 nuovi) passano — stessa modalita'
  di verifica della Fase 5 (g++/gtest locali, non `pio test -e native`).
  `exportToSerial()`/`importStart()`/`importFeedLine()` e i branch
  `export`/`import` in `MyMesh.cpp` non sono testabili nativamente
  (dipendono da `Serial`/Arduino, stesso limite della Fase 2 per
  `MyMeshReplyChannel`): solo revisione manuale, non compilati ne' eseguiti
  su hardware reale in questo passaggio — da verificare su banco prima
  dell'uso: un ciclo `export` → `erase`/riflash → `import` → confronto con
  un nuovo `export` è il test end-to-end naturale, non ancora eseguito.
