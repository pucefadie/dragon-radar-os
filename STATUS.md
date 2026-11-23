# Dragon Radar OS — stato M1

**Branch:** `feat/espnow-m1-bidirectional` · **Baseline M1c fisica:** `0966740` · **PR #2:** draft. La [revisione AI #3](https://github.com/pucefadie/dragon-radar-os/issues/3#issuecomment-7100886934) ha segnalato isolamento dei flag, limiti della prova CCMP e verifica degli errori PMK. La procedura di riferimento è [M1C_BENCH_TEST.md](M1C_BENCH_TEST.md).

| Stato | Evidenza |
|---|---|
| **PASS — M1b software** | DRA1 bidirezionale per ID 1 con ESP-NOW unicast cifrato, handshake HMAC, replay window, ACK/retry e utility cablata per NVS. Modalità DEV separata. Nove gruppi host e sei build erano passati al [checkpoint M1b](https://github.com/pucefadie/dragon-radar-os/blob/a43bfe7/STATUS.md). |
| **PASS — preparazione M1c** | Rieseguiti otto test C++ e il test del generatore con record sintetici. Verificati hash dei sei binari M1b, coppie dei backup integrali S3/C6, layout e fit delle immagini app per il banco. Bootstrap S3 storico con rollback attivo. Build C6 standard e provisioning `no_ota` riuscite e corrispondenti alla tabella C6 archiviata. |
| **FAIL — profilo C6 M1b rispetto alla scheda** | Le immagini C6 M1b erano valide per il proprio profilo a due slot, ma quella tabella non corrisponde a quella archiviata per il C6 fisico. Usare solo i nuovi candidati app C6 `no_ota`, previa verifica corrente. |
| **PASS storico — DRB2 fisico, solo ricezione** | Ricevitore S3 minimale: 10 beacon validi confermati sul canale 6, senza errori CRC nei report completi, NVS immutata. Al termine OTA1 selezionato con sequenza 4 VALID, OTA0 integro. Non è una prova DRA1. |
| **PASS — M1c fisico, profilo banco** | Provisioning e DRA1 verificati su S3/C6 con PMK comune derivata. Handshake, ACK/retry, tag errato, replay, perdita e recupero verificati. L'evidenza fisica M1d e la cattura indipendente CCMP restano aperte. |

## Audit storico S3 con batteria e motore collegati

Questa configurazione precede il preflight S3 in sola USB riportato in fondo. Per le operazioni correnti seguire [M1C_BENCH_TEST.md](M1C_BENCH_TEST.md).

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

1. Autorizzare il primo collaudo M1d con i DRC1 esistenti, percorso DRA1 standard nelle due immagini radio di banco. Acquisire backup correnti e scrivere solo OTA1 S3 e app0 C6.
2. Verificare in una sessione separata il provisioning coordinato DRC2 via USB, la selezione `record_version=2` e gli stessi casi DRA1. Conservare le impostazioni e i backup pre-installazione.
3. Acquisire un'osservazione radio indipendente di CCMP e verificare il ritorno al firmware precedente. Rivalutare alimentazione e attuatori se cambia il banco.

[M1C_BENCH_TEST.md](M1C_BENCH_TEST.md) · [SECURITY_PLAN.md](SECURITY_PLAN.md). **M1 resta aperta** e la PR resta draft.

## Storico preflight live M1c — primo tentativo

- **PASS enumerazione USB.** S3 e C6 erano presenti sulle porte storiche COM11 e COM12; ciascun identificativo USB padre coincideva con quello registrato. Nessuna operazione su COM5. La presenza USB da sola non identifica la modalità di avvio.
- **FAIL accesso ROM senza reset.** Con esptool 5.3.1, un tentativo per target, `--before no-reset --after no-reset --no-stub`: S3 `Write timeout`; C6 `No serial data received`. Le porte continuavano a risultare presenti. Nessuna risposta alla sincronizzazione: stato di avvio non determinato su entrambe. Nessun reset richiesto, nessuno stub caricato.
- **NOT RUN letture correnti.** Chip/protezioni/eFuse live, layout, app, NVS, otadata e backup attuali. Nessun flash, erase, provisioning o scrittura su scheda. Le coppie di backup storici S3 e C6 sono ancora identiche. I log seriali e gli identificativi USB restano privati.
- **Intervento:** lasciare il S3 nello stato attuale con batteria e motore collegati. La procedura Waveshare usa BOOT durante un reset o ciclo di alimentazione; sul radar non eseguirla finché non ci sia evidenza elettrica adeguata per quella transizione con il motore collegato. Preferire indicazioni Waveshare per la revisione esatta, senza smontaggio. Per il C6, se è alimentato solo via USB e gli attuatori esterni sono isolati, entrare in ROM con BOOT durante il collegamento USB dati; confermare quando pronto. Ripetere l'enumerazione e il test senza reset prima delle doppie letture. Nessuna approvazione a scrivere.

## Storico preflight C6 in ROM — cattura parziale

- **PASS** C6 riconosciuto su COM12, risposta ROM ESP32-C6FH4 rev. 0.2, flash 4 MiB. Secure Boot e Flash Encryption disattivati, download ROM abilitato. Inventario eFuse acquisito in lettura.
- **PASS** due letture del settore tabella identiche, MD5 interno valido, uguale al backup storico. Layout reale a slot singolo `no_ota`: app0 `0x10000/0x200000`. I due candidati C6 `no_ota` coincidono con la tabella letta e rientrano nello slot.
- **FAIL** due backup live integrali. Le letture lunghe da ROM si interrompono. Con stub RAM esplicitamente autorizzato è stato letto un blocco da 512 KiB a `0x0`; i trasferimenti successivi si sono interrotti anche con un blocco ridotto. Blocco parziale conservato, non idoneo al rollback. La sua tabella, bootloader e otadata coincidono con lo storico; NVS e prefisso app differiscono. La coppia integrale storica è ancora identica, ma non ripristina lo stato corrente.
- **NOT RUN** installazione, provisioning e rollback. Nessun reset richiesto, nessuna scrittura persistente. C6 lasciato con stub in RAM, S3 intatto. Prima di qualunque flash occorrono trasporto C6 stabile e due copie correnti complete e identiche. Sul S3 resta da concordare il percorso ROM manuale non invasivo per la revisione esatta, con verifica elettrica prima di qualsiasi reset. Log e dati restano privati.

## Preflight C6 — doppia cattura USB riuscita

- **PASS** due letture integrali indipendenti da 4 MiB con il solo cavo USB. Riutilizzato lo stub Espressif già in RAM. Il lettore sul PC ha richiesto pacchetti da 1 KiB con una sola conferma in sospeso. MD5 del dispositivo verificato per entrambe le letture, SHA-256 uguale e confronto byte per byte riuscito. Il trasporto di lettura separa il nuovo risultato dai tentativi parziali sopra.
- **PASS** confronto con lo storico e recupero offline. Bootloader, tabella, otadata, SPIFFS e coredump coincidono. App0 e NVS differiscono. Estratto il nuovo app0 a `0x10000/0x200000`, con checksum e hash dell'immagine ESP32-C6 validi. Estratta NVS a `0x9000/0x5000` per un eventuale ripristino separato. Dump, hash e piano operativo sono conservati fuori da Git.
- **NOT RUN** scrittura C6 e prova di ritorno da ROM. Lo slot singolo richiede un'eventuale installazione app limitata ad app0, con rilettura prima del riavvio. Il profilo USB verificato copre le letture. Se l'app è stata riavviata dopo il backup, riverificare le regioni correnti prima di autorizzare una scrittura.
- **Storico S3** durante la cattura C6 il radar non si enumerava. In seguito è entrato manualmente in ROM con sola USB, come riportato nel preflight S3 qui sotto.

## Diagnostica DRA1 — retry e rifiuti

- **PASS host** nove gruppi, più il test DRA1 con AddressSanitizer e UndefinedBehaviorSanitizer. I test coprono perdita di READY e ACK, retry e deduplicazione, tag HMAC errato, sessione precedente, riavvio di entrambe le parti e scadenza senza rinnovo da un duplicato. Il riepilogo seriale standard espone quattro contatori numerici senza identificativi o chiavi.
- **PASS audit S3 banco** controllati marker, esclusione degli attuatori, separazione da DEV e ordine dello startup. La procedura e il significato dei contatori sono in [M1C_BENCH_TEST.md](M1C_BENCH_TEST.md).
- **PASS S3 build** standard, DEV e banco su Linux con Arduino ESP32 3.3.12. I binari standard e banco contengono i contatori DRA1; il marcatore DEV compare soltanto nella variante DEV.
- **C6 build:** la prima CLI Linux non aveva la libreria target `esp32c6-libs`. Ripetute con successo le compilazioni standard e DEV in staging Windows isolato, core Arduino ESP32 3.3.12 e `no_ota`. Il binario standard include i nuovi contatori ed esclude DEV. Entrambi i binari rientrano nello slot e le tabelle generate coincidono con la tabella live. Per la verifica riproducibile è stato usato lo sketch C6 tracciato, preservando la variante antenna interna locale.
- **NOT RUN** collaudo DRA1 fisico, provisioning e rollback. La sonda USB sul radar può riprendere quando compare una sua porta. Le prove radio e le scritture restano sessioni separate da autorizzare.

| Snapshot radio precedente alla fixture | Byte | SHA-256 |
|---|---:|---|
| S3 standard | 1782224 | `ed98c58aa9b512b59ea63831b67a99862fbe455f8c264fd85b047aaaef2b5037` |
| S3 DEV | 1779120 | `39fd9a262beec14dd37ce50cc309797651cdc9c12290557093b008846dfbd6a5` |
| S3 banco radio | 878848 | `f40ededc073f939f4a1c1d056e51f02e61a1b6b9be20c8dfa4cc770b83d29b86` |
| C6 standard no_ota | 971136 | `c8ec66e56009f9d93f64dd8fcf36ee5ba73c8114c5bf264ad5ec58f7feb5d575` |
| C6 DEV no_ota | 967360 | `c42fc411fc957d88a5a8824655aad07a39a14d5a29330533c818b29ef17ecba2` |

## Preflight S3 in ROM — configurazione USB senza batteria

- **PASS** porte riconfermate, S3 su COM11 e C6 su COM12. Dopo il timeout iniziale, ingresso ROM manuale confermato dall'operatore e dalla sonda. ESP32-S3 rev. 0.2 e flash 16 MiB. Inventario eFuse in sola lettura: Secure Boot e Flash Encryption disattivati, download via USB disponibile. Nessun reset automatico, stub S3 o scrittura persistente.
- **PASS** doppia lettura della tabella partizioni, di NVS, di otadata e della prima pagina di ciascuno slot. Tabella con MD5 valido e identica al backup originale. NVS e otadata identiche al termine del trial minimale. Entry con CRC corretti: OTA0 seq 3 VALID a `0xe000`, OTA1 seq 4 VALID a `0xf000`. Il prossimo boot normale selezionerebbe OTA1. Applicazione effettivamente avviata e canale restano **NOT RUN**.
- **PASS** entrambe le copie originali da 16 MiB e due dump correnti completi per ciascuno slot da 3 MiB, identici byte per byte e per SHA-256. Anche il doppio MD5 della ROM concorda. Bootloader/tabella e OTA0 coincidono con l'originale. OTA1 coincide con la ricostruzione da readback dell'app minimale, padding dell'ultimo settore e coda salvata prima del trial. Il timeout seriale fisso sul PC evita riconfigurazioni per ogni richiesta ROM da 64 byte e ha portato la lettura a circa 85 KiB/s. Copie, hash e piano di recupero restano privati.
- **PASS compatibilità** verificati SHA-256, chip, tabelle e intervalli di erase dei quattro candidati radio e provisioning S3/C6. OTA1 già selezionato consente di pianificare la sostituzione dell'app S3 senza aggiornare otadata. **NOT RUN** programmazione, provisioning, rollback e collaudo fisico.
- **PASS revisione DRA1, checkpoint `f03c27a`** nove gruppi host, più sanitizzatori. Il caso verifica che un retry rifiutato dalla coda TX consumi un tentativo senza incrementare `probe_retry_queued`. Il loop applicativo possiede macchina di stato e contatori. La callback ESP-NOW accoda i dati e usa un contatore atomico distinto per i drop.

## Fixture DRA1 opzionale per M1c

- **PASS software** nove gruppi host standard, test della fixture e ripetizione con AddressSanitizer e UndefinedBehaviorSanitizer. Verificati ACK autenticato scartato una volta, retry che mantiene il probe originale, rifiuto HMAC sulla sfera, rifiuto del replay nella sessione corrente e scarto dopo un nuovo handshake. Risultato delle protezioni di build **PASS** per varianti incompatibili.
- **PASS S3 fixture** Arduino ESP32 3.3.12, app da 879904 byte, SHA-256 `e3df37aa040a57cd9de89d930744c7de135861b37f1308b172310de047616bf5`. Marcatori, checksum e hash immagine, ordine GPIO16 LOW e assenza dei percorsi attuatori verificati. L'app rientra in OTA1; intervallo di erase arrotondato `0x310000–0x3e6fff`.
- **PASS build di regressione** S3 standard, DEV, banco e provisioning, C6 standard e DEV `no_ota`. Insieme alla fixture, tutte e sette le immagini hanno checksum e hash d'immagine validi, tabella corrispondente al preflight e spazio sufficiente nello slot. La variante fixture è l'unica con il relativo marker e i comandi di test. Il C6 è stato compilato dallo sketch tracciato con la toolchain Windows, preservando la variante locale.
- **NOT RUN** installazione, provisioning, prova radio e recupero. Riconfermate le identità USB su COM11 e COM12 tramite Windows. Le azioni della [fixture di banco](M1C_BENCH_TEST.md) richiedono installazione approvata. La verifica indipendente di CCMP richiede una cattura radio.

| Altri candidati di banco ricompilati | Byte | SHA-256 |
|---|---:|---|
| S3 banco ordinario | 878848 | `ebc01cd8ac18e631272cd525a7cc393e4a05ba1dff95701b615a4853d9b99fea` |
| S3 provisioning | 379968 | `654693ac1244f7173008e4d98972e15a6493151a17064b63e2fcab7cba3e979f` |
| C6 standard no_ota | 971136 | `65c59d313b5081265b8612be8539ef9a115036eb5efc9489b3c55f336e8c0b48` |

## Primo collaudo M1c autorizzato

- **PASS preflight e installazione** riconfermati i due dump integrali C6 e le catture S3. Installate solo app negli slot `0x10000` C6 e `0x310000` S3. L'utility e l'app radio sono state rilette e confrontate byte per byte su entrambe le schede. Usato un avvio C6 protetto con `--wrap=initArduino` per fermarsi se NVS non si inizializza. La build radio usa la configurazione Wi-Fi in RAM.
- **PASS provisioning** entrambe le utility hanno restituito `RADIO_PROVISION_OK` e il controllo offline della NVS riletta ha ritrovato i record attesi. Al primo avvio S3 il reset USB via RTS era rimasto in download: NVS e otadata erano invariati. Il reset completo watchdog S3 ha avviato correttamente l'utility. Nessuna ripetizione alla cieca dell'invio NVS.
- **PASS radio DRB2, FAIL DRA1 con PMK distinte** entrambi i peer riportano `configured=1 aligned=1` sul canale 6. Nel tentativo con diagnostica di trasporto il radar ha accettato 57 beacon DRB2 e ottenuto 69 esiti TX di livello MAC riusciti per frame DRA1. Il C6 ha mostrato zero frame da 80 byte nella callback e zero conferme DRA1. Entrambi sono rimasti in `state=2`. Il driver TX da solo non dimostra la ricezione DRA1.
- **PASS verifica NVS** record di banco verificati senza esporre valori. Le sole voci preesistenti differenti rilevate dal parser NVS appartengono al Wi-Fi/PHY. Sul C6 bootloader, tabella, otadata, SPIFFS e coredump coincidono con la cattura pre-installazione. S3 otadata coincide con il preflight.
- **PASS software** nove gruppi host e fixture. Il test TX distingue esito API, esito callback, peer mancante, buffer pieno e frame scaduto. Corretto il confronto con il MAC di fabbrica sul C6 usando un buffer EUI-64.

### Profilo di banco con PMK comune derivata

- **PASS DRA1 su canale 6** con `M1C_LINK_PMK_PROBE=1` su entrambe le schede. Controllo applicativo HMAC e peer ESP-NOW con LMK/cifratura attivi. NVS di provisioning mantenuta senza reinviare i record. Entrambi Paired per 60 s, conferme S3 da 1 a 21 e C6 da 2 a 22. Nessun drop o stallo nel campione.
- **PASS fixture e canale** un ACK scartato al radar ha causato un retry e un duplicato riconosciuto dalla sfera. Un probe con HMAC alterato ha incrementato `auth_rejected` sul C6. Il passaggio del solo S3 al canale 1 ha portato entrambi a Lost e il ritorno al canale 6 ha rigenerato il pairing. Un probe della sessione precedente ha incrementato `stale_session` sul C6.
- **PASS riavvii e recupero** C6 tenuto in ROM per almeno 20 s, radar in Lost, poi ritorno a Paired su entrambi all'avvio del C6. Riavvio separato S3 con C6 acceso, nuova sessione e conferme riprese. Nessun reinvio delle associazioni.
- **Build installate e rilette byte per byte** S3 in OTA1 da 880768 byte, SHA-256 `0464142cc572571f13df612c75290f78f42f387d2f604b0aafac13a51503df64`; C6 in app0 da 964800 byte, SHA-256 `a597e6940e72e67c7be252c0ec4efae2cfc5dadfe906f40d6681f4f48021f8d0`. I metadati OTA S3 sono rimasti invariati.
- **PASS integrità finale** S3 bootloader/tabella, OTA0, otadata, FAT e coda di OTA1 oltre l'area toccata coincidono con le copie di riferimento. C6 bootloader/tabella, otadata, SPIFFS, coredump e coda di app0 coincidono. Le due NVS sono state lette due volte e conservano i record di banco. Al di fuori del pairing le sole differenze logiche riguardano Wi-Fi/PHY. Dopo il controllo entrambe le schede sono tornate Paired sul canale 6.
- **Aperto al termine di M1c** configurazione PMK dei binari standard, vista separata del bit Protected/CCMP tramite sniffer e ripristino effettivo dei firmware originali.

## M1d — PMK del firmware standard

- **PASS implementazione** DRC2 con una PMK casuale comune al dominio e LMK/HMAC distinte per coppia. Il parser preferisce DRC2 e rifiuta un valore invalido presente. Solo in assenza di DRC2 legge DRC1. La derivazione DRC1 in RAM usa il contesto del banco riuscito. Firmware e utilità non riscrivono DRC1. L'utility separata aggiunge DRC2 solo se assente. Il flag sperimentale è stato rimosso dalle build.
- **PASS host** nove gruppi esistenti, suite DRA1 standard e fixture con ASan/UBSan, utility USB in cinque scenari con e senza sanitizzatori. Coperti precedenza e fallimento DRC2, compatibilità DRC1, HMAC con chiave errata, ACK, retry, duplicati, replay e riavvio. I test host non esercitano la radio CCMP.
- **PASS recupero offline** due backup originali S3 da 16 MiB, due snapshot pre-M1c C6 da 4 MiB e hash dei readback applicativi del collaudo M1c riverificati. Prima di una futura scrittura servono due nuove catture delle regioni live. Il recupero dell'app C6 passa da ROM e dallo stesso slot.
- **NOT RUN hardware M1d** nessun contatto con le schede in questa fase. I PASS fisici sopra sono del checkpoint M1c. Restano collaudo DRC1 con i nuovi firmware radio standard, prova coordinata DRC2, ritorno fisico e cattura CCMP. Il profilo standard di prodotto S3, con UI e periferiche, richiede una prova distinta da quella del percorso radio protetto.

### Build verificate al checkpoint di pausa

Arduino ESP32 3.3.12. Verificati formato, hash, marker e corrispondenza alle tabelle partizioni archiviate per i candidati sotto. S3 standard completo, S3 DEV, fixture S3 e C6 DEV restano da verificare al rientro.

| Candidato | Byte | SHA-256 |
|---|---:|---|
| s3-bench | 879776 | `ad5f266a3cea3b32137095e5693452bccce01ce0948d7886f5bb76c4988e3f40` |
| s3-provision | 378304 | `41f4320af57026ea17e722370a98523bfa75d7ca545082520e45851927decdb7` |
| c6-radio | 965008 | `31e5fbc62c5936098530e5baff4654e33e96b1f321dfdb1176f4c9e8069f5eed` |
| c6-provision | 245312 | `a4b8ea25bc2f4b609a586e297386cdf441b67f797e1298fe7f771ef3cbd455f5` |
| c6-standard | 972416 | `11ca28197d34723d44da45de2aff5f76e79c90edd2d337f2d97462af32404acb` |
