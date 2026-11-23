# Dragon Radar OS — stato M1

**Branch:** `feat/espnow-m1-bidirectional` · **Baseline M1b:** `a43bfe7` · **Baseline piano M1c:** `bbfd0ae` · **PR #2:** draft. Il checkpoint corrente verifica i rilievi della [revisione AI #3 sul preflight](https://github.com/pucefadie/dragon-radar-os/issues/3#issuecomment-6991603548).

| Stato | Evidenza |
|---|---|
| **PASS — M1b software** | DRA1 bidirezionale per ID 1 con ESP-NOW unicast cifrato, handshake HMAC, replay window, ACK/retry e utility cablata per NVS. Modalità DEV separata. Nove gruppi host e sei build erano passati al [checkpoint M1b](https://github.com/pucefadie/dragon-radar-os/blob/a43bfe7/STATUS.md). |
| **PASS — preparazione M1c** | Rieseguiti otto test C++ e il test del generatore con record sintetici. Verificati hash dei sei binari M1b, coppie dei backup integrali S3/C6, layout e fit delle immagini app per il banco. Bootstrap S3 storico con rollback attivo. Build C6 standard e provisioning `no_ota` riuscite e corrispondenti alla tabella C6 archiviata. |
| **FAIL — profilo C6 M1b rispetto alla scheda** | Le immagini C6 M1b erano valide per il proprio profilo a due slot, ma quella tabella non corrisponde a quella archiviata per il C6 fisico. Usare solo i nuovi candidati app C6 `no_ota`, previa verifica corrente. |
| **PASS storico — DRB2 fisico, solo ricezione** | Ricevitore S3 minimale: 10 beacon validi confermati sul canale 6, senza errori CRC nei report completi, NVS immutata. Al termine OTA1 selezionato con sequenza 4 VALID, OTA0 integro. Non è una prova DRA1. |
| **NOT RUN — collaudo M1c fisico** | Il preflight C6 è avanzato fino alle doppie letture integrali riportate qui sotto. Sul radar resta aperto l'ingresso ROM. Provisioning, DRA1, ACK, replay, perdita e recupero non sono stati provati sulle schede. I casi negativi fisici richiedono ancora una fixture dedicata. |

## S3 con batteria e motore collegati

**PASS offline:** compilate due immagini S3 separate con `M1C_BENCH_MODE=1` attraverso `scripts/build_m1c_bench.sh` (opzione `M1C_BENCH_PROVISION=0|1`). L'audit ripetibile `scripts/audit_m1c_bench.py` conferma marker, variante, assenza dei percorsi software per attuatori, sostituzione effettiva di `initArduino` e ordine GPIO16 LOW → configure → readback → NVS. La build provisioning è solo USB. La radio S3 usa STA senza AP e non cambia le impostazioni Wi-Fi salvate; la calibrazione o manutenzione NVS può scrivere dopo un avvio autorizzato. Nessun erase-and-retry di NVS nello startup banco S3. Il firmware standard e gli artefatti precedenti sono intatti.

**PASS offline:** 9 gruppi host rieseguiti, controllo immagine e tabella su entrambe le build S3, coppie dei backup integrali storici riverificate. **FAIL software:** nessuno a fine verifica. **NOT RUN / gate:** misura del livello elettrico con batteria e USB collegate, reset, ROM live e boot delle nuove app. La documentazione non dimostra un LOW continuo di GPIO16 prima del software. Niente reset automatico o avvio app sul radar. Se non è già stabile in ROM, differire il preflight S3.

## Immagini applicative del primo snapshot M1c

I file sono locali e non programmati. Le immagini S3 standard/provisioning M1b non sono approvate per questa prova. C6 `PartitionScheme=no_ota`, antenna esterna e canale 6 per l'app radio. Core Arduino ESP32 3.3.12. I candidati devono essere approvati separatamente dopo preflight in sola lettura e verifica del gate elettrico.

| Immagine | Byte | SHA-256 |
|---|---:|---|
| S3 banco radio | 878608 | `d2b2f055ca29e98b7a2cc14687115b42199625ac2f631557c00075da4c85ea0c` |
| S3 banco provisioning | 379968 | `b947022a81edb73beb801332da96c4665859ec819c2a36a11b4447165a2f40df` |
| C6 standard no_ota | 970880 | `89ec01d90823313771fe393489384f512bd295670d7f7f1630a5563e19f9b61f` |
| C6 provisioning no_ota | 263008 | `705a737d66416b8fff38a276d79981177f50ea5f208485db3603b931044b7d64` |

## Prossime tre azioni

1. L'utente conferma USB, stato ROM già attivo e stabilità del S3 con batteria/motore collegati. Nessuno smontaggio. C6 solo USB, antenna collegata e attuatori esterni isolati.
2. Eseguire il preflight consentito sulla scheda in ROM senza reset, doppie letture, confronto SHA-256 e piano di recupero per scheda. Se la ROM S3 non è già disponibile o emergono transizioni/rischi, lasciare il S3 fermo. Il backup integrale storico C6 precede l'app DEV osservata per ultima.
3. Con l'evidenza elettrica mancante risolta e dopo approvazione distinta delle immagini, del provisioning e del recupero, programmare e collaudare DRA1. I casi negativi fisici richiedono una fixture separata.

[M1C_BENCH_TEST.md](M1C_BENCH_TEST.md) · [SECURITY_PLAN.md](SECURITY_PLAN.md). **M1 resta aperta** e la PR resta draft.

## Preflight live M1c — primo tentativo

- **PASS enumerazione USB.** S3 e C6 erano presenti sulle porte storiche COM11 e COM12; ciascun identificativo USB padre coincideva con quello registrato. Nessuna operazione su COM5. La presenza USB da sola non identifica la modalità di avvio.
- **FAIL accesso ROM senza reset.** Con esptool 5.3.1, un tentativo per target, `--before no-reset --after no-reset --no-stub`: S3 `Write timeout`; C6 `No serial data received`. Le porte continuavano a risultare presenti. Nessuna risposta alla sincronizzazione: stato di avvio non determinato su entrambe. Nessun reset richiesto, nessuno stub caricato.
- **NOT RUN letture correnti.** Chip/protezioni/eFuse live, layout, app, NVS, otadata e backup attuali. Nessun flash, erase, provisioning o scrittura su scheda. Le coppie di backup storici S3 e C6 sono ancora identiche. I log seriali e gli identificativi USB restano privati.
- **Intervento:** lasciare il S3 nello stato attuale con batteria e motore collegati. La procedura Waveshare usa BOOT durante un reset o ciclo di alimentazione; sul radar non eseguirla finché non ci sia evidenza elettrica adeguata per quella transizione con il motore collegato. Preferire indicazioni Waveshare per la revisione esatta, senza smontaggio. Per il C6, se è alimentato solo via USB e gli attuatori esterni sono isolati, entrare in ROM con BOOT durante il collegamento USB dati; confermare quando pronto. Ripetere l'enumerazione e il test senza reset prima delle doppie letture. Nessuna approvazione a scrivere.

## Preflight C6 in ROM — cattura parziale

- **PASS** C6 riconosciuto su COM12, risposta ROM ESP32-C6FH4 rev. 0.2, flash 4 MiB. Secure Boot e Flash Encryption disattivati, download ROM abilitato. Inventario eFuse acquisito in lettura.
- **PASS** due letture del settore tabella identiche, MD5 interno valido, uguale al backup storico. Layout reale a slot singolo `no_ota`: app0 `0x10000/0x200000`. I due candidati C6 `no_ota` coincidono con la tabella letta e rientrano nello slot.
- **FAIL** due backup live integrali. Le letture lunghe da ROM si interrompono. Con stub RAM esplicitamente autorizzato è stato letto un blocco da 512 KiB a `0x0`; i trasferimenti successivi si sono interrotti anche con un blocco ridotto. Blocco parziale conservato, non idoneo al rollback. La sua tabella, bootloader e otadata coincidono con lo storico; NVS e prefisso app differiscono. La coppia integrale storica è ancora identica, ma non ripristina lo stato corrente.
- **NOT RUN** installazione, provisioning e rollback. Nessun reset richiesto, nessuna scrittura persistente. C6 lasciato con stub in RAM, S3 intatto. Prima di qualunque flash occorrono trasporto C6 stabile e due copie correnti complete e identiche. Sul S3 resta da concordare il percorso ROM manuale non invasivo per la revisione esatta, con verifica elettrica prima di qualsiasi reset. Log e dati restano privati.

## Preflight C6 — doppia cattura USB riuscita

- **PASS** due letture integrali indipendenti da 4 MiB con il solo cavo USB. Riutilizzato lo stub Espressif già in RAM. Il lettore sul PC ha richiesto pacchetti da 1 KiB con una sola conferma in sospeso. MD5 del dispositivo verificato per entrambe le letture, SHA-256 uguale e confronto byte per byte riuscito. Il trasporto di lettura separa il nuovo risultato dai tentativi parziali sopra.
- **PASS** confronto con lo storico e recupero offline. Bootloader, tabella, otadata, SPIFFS e coredump coincidono. App0 e NVS differiscono. Estratto il nuovo app0 a `0x10000/0x200000`, con checksum e hash dell'immagine ESP32-C6 validi. Estratta NVS a `0x9000/0x5000` per un eventuale ripristino separato. Dump, hash e piano operativo sono conservati fuori da Git.
- **NOT RUN** scrittura C6 e prova di ritorno da ROM. Lo slot singolo richiede un'eventuale installazione app limitata ad app0, con rilettura prima del riavvio. Il profilo USB verificato copre le letture. Se l'app è stata riavviata dopo il backup, riverificare le regioni correnti prima di autorizzare una scrittura.
- **FAIL / gate S3** un controllo precedente aveva confermato COM11 ma la sonda ROM senza reset era scaduta. Al nuovo collegamento Windows rileva il solo C6 e il radar non si enumera. L'utente ha riferito di aver isolato pulsanti esterni, bussola e motore e di vedere PWR e BOOT sulla Waveshare. La batteria resta collegata. Prima verificare l'USB con lo stesso cavo dati e la stessa porta del C6. Se il radar si enumera ma la ROM non risponde, serve un percorso manuale documentato per entrare in ROM con PWR e BOOT. Nessun reset S3 eseguito.

## Diagnostica DRA1 — retry e rifiuti

- **PASS host** nove gruppi, più il test DRA1 con AddressSanitizer e UndefinedBehaviorSanitizer. I test coprono perdita di READY e ACK, retry e deduplicazione, tag HMAC errato, sessione precedente, riavvio di entrambe le parti e scadenza senza rinnovo da un duplicato. Il riepilogo seriale standard espone quattro contatori numerici senza identificativi o chiavi.
- **PASS audit S3 banco** controllati marker, esclusione degli attuatori, separazione da DEV e ordine dello startup. La procedura e il significato dei contatori sono in [M1C_BENCH_TEST.md](M1C_BENCH_TEST.md).
- **PASS S3 build** standard, DEV e banco su Linux con Arduino ESP32 3.3.12. I binari standard e banco contengono i contatori DRA1; il marcatore DEV compare soltanto nella variante DEV.
- **C6 build:** la prima CLI Linux non aveva la libreria target `esp32c6-libs`. Ripetute con successo le compilazioni standard e DEV in staging Windows isolato, core Arduino ESP32 3.3.12 e `no_ota`. Il binario standard include i nuovi contatori ed esclude DEV. Entrambi i binari rientrano nello slot e le tabelle generate coincidono con la tabella live. Per la verifica riproducibile è stato usato lo sketch C6 tracciato, preservando la variante antenna interna locale.
- **NOT RUN** collaudo DRA1 fisico, provisioning e rollback. La sonda USB sul radar può riprendere quando compare una sua porta. Le prove radio e le scritture restano sessioni separate da autorizzare.

| Applicazione di questo checkpoint | Byte | SHA-256 |
|---|---:|---|
| S3 standard | 1782224 | `ed98c58aa9b512b59ea63831b67a99862fbe455f8c264fd85b047aaaef2b5037` |
| S3 DEV | 1779120 | `39fd9a262beec14dd37ce50cc309797651cdc9c12290557093b008846dfbd6a5` |
| S3 banco radio | 878848 | `f40ededc073f939f4a1c1d056e51f02e61a1b6b9be20c8dfa4cc770b83d29b86` |
| C6 standard no_ota | 971136 | `c8ec66e56009f9d93f64dd8fcf36ee5ba73c8114c5bf264ad5ec58f7feb5d575` |
| C6 DEV no_ota | 967360 | `c42fc411fc957d88a5a8824655aad07a39a14d5a29330533c818b29ef17ecba2` |
