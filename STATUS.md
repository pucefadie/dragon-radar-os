# Dragon Radar OS — stato di sviluppo

**Branch:** `feat/espnow-m1-bidirectional` · **Baseline M1c fisica:** `0966740` · **PR #2:** draft. La [revisione AI #3](https://github.com/pucefadie/dragon-radar-os/issues/3#issuecomment-7102333493) ha segnalato i controlli su compatibilità DRC1/DRC2, sequenza PMK/peer e disponibilità dell'API NVS. La procedura di riferimento è [M1C_BENCH_TEST.md](M1C_BENCH_TEST.md).

| Stato | Evidenza |
|---|---|
| **PASS — M1b software** | DRA1 bidirezionale per ID 1 con ESP-NOW unicast cifrato, handshake HMAC, replay window, ACK/retry e utility cablata per NVS. Modalità DEV separata. Nove gruppi host e sei build erano passati al [checkpoint M1b](https://github.com/pucefadie/dragon-radar-os/blob/a43bfe7/STATUS.md). |
| **PASS — preparazione M1c** | Rieseguiti otto test C++ e il test del generatore con record sintetici. Verificati hash dei sei binari M1b, coppie dei backup integrali S3/C6, layout e fit delle immagini app per il banco. Bootstrap S3 storico con rollback attivo. Build C6 standard e provisioning `no_ota` riuscite e corrispondenti alla tabella C6 archiviata. |
| **FAIL — profilo C6 M1b rispetto alla scheda** | Le immagini C6 M1b erano valide per il proprio profilo a due slot, ma quella tabella non corrisponde a quella archiviata per il C6 fisico. Usare solo i nuovi candidati app C6 `no_ota`, previa verifica corrente. |
| **PASS storico — DRB2 fisico, solo ricezione** | Ricevitore S3 minimale: 10 beacon validi confermati sul canale 6, senza errori CRC nei report completi, NVS immutata. Al termine OTA1 selezionato con sequenza 4 VALID, OTA0 integro. Non è una prova DRA1. |
| **PASS — M1c fisico, profilo banco** | Provisioning e DRA1 verificati su S3/C6 con PMK comune derivata. Handshake, ACK/retry, tag errato, replay, perdita e recupero verificati. |
| **COMPLETATA — M1d, DRC1** | Percorso radio standard nel profilo di banco, 60 s Paired sul canale 6, 20 nuove conferme applicative per scheda. Recupero dopo entrambi i riavvii e associazioni DRC1 identiche dopo la prova. |
| **COMPLETATA — M2a, UI Trial offline** | Radar e C6 M1d Paired per 60 s sul canale 6, radar senza AP, conferme in aumento. NVS e funzionamento della UI verificati nel collaudo precedente dello stesso candidato. |

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

## Prossime azioni M2c

1. Dopo autorizzazione, verificare la sola app S3 M2c con la sfera 1 M3b e la configurazione radio attuale.
2. In Aperto, acquisire un fix reale e salvare una posizione per una sfera associata. Verificare distanza, direzione, zoom e orientamento.
3. Verificare il fallback RSSI, la conservazione della posizione dopo riavvio e la regressione di Demo e Chiuso.

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
- **PASS installazione M1d** doppio backup corrente per entrambe le schede. Installati e riletti byte per byte il solo candidato S3 banco in OTA1 a `0x310000` e il C6 radio in app0 a `0x10000`. OTA0 originale preservato, OTA1 ancora selezionato con seq 4 VALID e otadata invariato. I backup e i log restano privati.
- **PASS DRA1 fisico con DRC1** entrambi i dispositivi Paired 60 s sul canale 6 con PMK e peer inizializzati. Conferme S3 21 → 41 e C6 66 → 86, zero nuovi retry, drop RX o stalli TX nel campione.
- **PASS recupero fisico** C6 in ROM ha portato il radar a Lost. Al ritorno del C6, conferme S3 90 → 92 e C6 2 → 4. Dopo il riavvio separato S3, contatore radar ripartito da 92 a 7, con conferme S3 7 → 9 e C6 14 → 16. Nessun nuovo rifiuto di autenticazione, drop o stallo TX nei campioni di recupero. La seriale USB Windows ha richiesto una nuova apertura dopo il reset, con conferma del recupero lato applicazione.
- **PASS NVS dopo le prove** due letture identiche per scheda e CRC validi. I DRC1 coincidono byte per byte con i backup e con i record di banco, DRC2 assente. Le immagini NVS grezze differiscono da quelle di pre-installazione. Le sole differenze logiche sono quattro voci PHY sul S3 e una sul C6. Otadata invariato su entrambe le schede, entrambe tornate Paired dopo la lettura.
- **NOT RUN** provisioning DRC2 su hardware, rollback fisico, cattura indipendente CCMP e prova del radar completo con UI e periferiche.

### Build M1d verificate

Nove build **PASS** con Arduino ESP32 3.3.12. S3 compilato su Linux, C6 su Windows dallo sketch tracciato. Verificati sorgenti in staging, header di destinazione, checksum, hash d'immagine, marker, slot e tabella partizioni archiviata. Audit avvio protetto **PASS** per le varianti S3 banco, provisioning e fixture e C6 radio e provisioning. La configurazione standard non contiene comandi fixture, marker DEV o il vecchio flag PMK.

| Candidato | Byte | SHA-256 | Intervallo massimo di erase dell'app |
|---|---:|---|---|
| s3-bench | 879776 | `ad5f266a3cea3b32137095e5693452bccce01ce0948d7886f5bb76c4988e3f40` | `0x310000–0x3e6fff` |
| s3-provision | 378304 | `41f4320af57026ea17e722370a98523bfa75d7ca545082520e45851927decdb7` | `0x310000–0x36cfff` |
| s3-fixture | 880816 | `6db15d7913cd529580f6716fea5683f4f6fb8e235c07507ccf21083e14a8fbf0` | `0x310000–0x3e7fff` |
| s3-standard | 1783552 | `03ccf7f31e6449bb6d891a5b2168dfa9331a6cc92c881d22e2449384a0ba8c25` | `0x310000–0x4c3fff` |
| s3-dev | 1779232 | `0ecdbf91323a20debf15b6d6331de0360661f5980db19bb47a1ebff64cb2ec9d` | `0x310000–0x4c2fff` |
| c6-radio | 965008 | `31e5fbc62c5936098530e5baff4654e33e96b1f321dfdb1176f4c9e8069f5eed` | `0x10000–0xfbfff` |
| c6-provision | 245312 | `a4b8ea25bc2f4b609a586e297386cdf441b67f797e1298fe7f771ef3cbd455f5` | `0x10000–0x4bfff` |
| c6-standard | 972416 | `11ca28197d34723d44da45de2aff5f76e79c90edd2d337f2d97462af32404acb` | `0x10000–0xfdfff` |
| c6-dev | 967504 | `23d80357563bd9bbc7fb9379bad6c12fb2e927e489274eb01c0c6a1f06ae2914` | `0x10000–0xfcfff` |

## Integrazione DRA1 nel radar completo

- **PASS host** il servizio espone lo stato DRA1 con perdita, attesa handshake e disallineamento distinti. Verificati l'attesa fino a READY autenticato, il rifiuto di un tag errato, la perdita, il recupero, il riavvio e l'indipendenza dai beacon DRB2. La UI usa il servizio già in esecuzione nel loop S3.
- **PASS profilo S3 UI Trial** `RADAR_UI_TRIAL=1 scripts/build_linux.sh <directory-nuova>` compila display e touch con DRA1 standard. Un errore di inizializzazione NVS ferma l'avvio prima del core Arduino. La build intercetta anche la cancellazione integrale della partizione NVS richiesta dal core in caso di esaurimento pagine o versione incompatibile. Test host del guard e verifica delle chiamate effettive nell'ELF passati. La normale manutenzione NVS e gli aggiornamenti dei dati PHY possono avvenire durante l'avvio autorizzato.
- **PASS sei build** Arduino ESP32 3.3.12. S3 standard, DEV e UI Trial con `scripts/build_linux.sh`, banco con `scripts/build_m1c_bench.sh`. C6 standard e DEV con `scripts/build_beacon.sh 1 6 esp32:esp32:XIAO_ESP32C6:PartitionScheme=no_ota,CDCOnBoot=cdc` dallo sketch tracciato. Verificati checksum e hash d'immagine, target, tabella rispetto ai backup, ingombro e separazione dei profili. Passati anche ASan/UBSan su DRA1 standard e fixture e i cinque casi dell'utility USB.
- **NOT RUN prova fisica UI** dopo un futuro avvio autorizzato, aprire Radar e Debug → Wi-Fi e sfere. Verificare dal comando `beacons` il canale reale e il record DRC1. Sul canale 6 attendere `SFERA 1 AUTENTICATA` e 60 s di conferme in crescita, interrompere temporaneamente il C6 e osservare `SFERA 1 LINK PERSO` dopo la scadenza. Al ritorno controllare nuovo handshake, conferme e ripristino della dicitura. Con l'AP su un altro canale controllare `CANALE RF NON ALLINEATO`. Il firmware usa il canale effettivo e non cambia l'AP né forza il canale della STA. Verificare infine che DRC1 e otadata coincidano con le letture precedenti.

| Candidato per il collaudo UI | Byte | SHA-256 | Intervallo massimo di erase dell'app |
|---|---:|---|---|
| S3 UI Trial, app OTA1 | 1784096 | `0ee789eb49cc277548a1786665e4294a7c0eed233c18e95aadb8d35553a9b073` | `0x310000–0x4c3fff` |

### Preflight S3 UI Trial in ROM

- **PASS** S3 riconosciuto in download ROM con flash da 16 MiB. Bootloader, tabella partizioni e OTA0 originali verificati. Due letture di tabella, NVS, otadata e dell'intero slot OTA1 da 3 MiB coincidono byte per byte. OTA1 contiene l'app M1d attesa e resta selezionato con sequenza 4, stato VALID e CRC valido. Otadata coincide con il checkpoint M1d.
- **PASS** DRC1 identico al backup, DRC2 assente. Rispetto alla lettura M1d precedente, le sole differenze logiche in NVS riguardano tre voci PHY. Binario UI Trial verificato per target, checksum, hash, compatibilità della tabella e ingombro. Copie, hash delle letture e copia dei settori di recupero `0x310000–0x4c3fff` conservati fuori da Git.
- **NOT RUN** programmazione e avvio della UI. Al termine del preflight il radar è in modalità ROM.

### M2a — verifica storica con il requisito iniziale di NVS invariata

- **PASS in ROM** sul radar COM11, flash da 16 MiB. Bootloader, tabella e OTA0 corrispondono all'originale. OTA1 corrisponde alla copia M1d. NVS e otadata coincidono byte per byte con il preflight. Hash del candidato e manifest dei backup confermati. Il preflight ha usato la configurazione USB con batteria e attuatori scollegati riferita dall'operatore. COM11 conferma la connessione USB dati.
- **FAIL compatibilità del candidato con NVS invariata**. Il binario con hash `0ee789eb…` è compilato con `CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE=y` e contiene `esp_phy_store_cal_data_to_nvs`. Il guard intercetta la cancellazione integrale della partizione. Il precedente avvio radio M1d ha già prodotto variazioni PHY reali. Poiché OTA1 è selezionato, programmare il candidato lo renderebbe avviabile al successivo reset. Il radar è rimasto in ROM con M1d installato.
- **PASS offline** rieseguiti nove gruppi host, fixture DRA1, ASan/UBSan e cinque casi di provisioning simulato. Passato anche il test host del guard NVS.
- **NOT RUN** flash, rilettura post-flash, avvio UI, inizializzazione fisica del display, touch e DRA1 per 60 s con il candidato UI Trial.

Checklist operatore per il successivo collaudo M2a, dopo la risoluzione del vincolo NVS:

1. Alimentare via USB e lasciare batteria e attuatori scollegati. Verificare che il pannello mostri il Radar con testo leggibile e senza artefatti.
2. Aprire il launcher con il touch, entrare in Debug → Wi-Fi e sfere e tornare al Radar. Controllare che i tocchi cambino pagina e che le etichette del link siano visibili.
3. Correlare le etichette di link con le letture contemporanee del comando seriale `beacons` sul canale 6. Annotare separatamente il rendering visibile e l'esito applicativo DRA1.

### M2a — collaudo UI Trial con aggiornamenti PHY consentiti

- **PASS programmazione e rilettura** scritto il candidato autorizzato da 1.784.096 byte in OTA1 a `0x310000`. Ultimo settore interessato fino a `0x4c3fff`. Il readback coincide byte per byte e ha lo SHA-256 previsto. NVS identica al preflight dopo la scrittura e prima dell'avvio.
- **PASS avvio su USB** console stabile per 70 s. Il firmware riporta inizializzazione QSPI AMOLED e controller touch riuscita, VBUS presente, batteria assente e motore in SafeOff. Rendering visivo e risposta ai tocchi **NOT RUN**.
- **PASS configurazione DRA1, BLOCKED prova Paired** DRC1 caricato, PMK e peer inizializzati. AP associato sul canale 1 e associazione DRC1 sul 6. Il radar ha riportato `aligned=0 state=2 confirmed=0`. La prova Paired da 60 s richiede un canale comune.
- **FAIL vincolo NVS dopo l'avvio** due letture ROM identiche e CRC validi. DRC1 identico al backup e DRC2 assente. Oltre a quattro differenze logiche PHY, il confronto ha rilevato quattro differenze nel namespace STA `nvs.net80211`, sui record `sta.apinfo` e `sta.chan`. Il risultato supera il vincolo PHY-only. Bootloader, tabella partizioni, OTA0, otadata e immagine OTA1 verificati dopo l'avvio.
- Prova interrotta con il radar in ROM e UI Trial installato. Il C6 ha conservato la configurazione M1d. Nessun rollback eseguito. Log e copie NVS conservati fuori da Git.

### M2a — UI Trial offline

- `sta.apinfo` e `sta.chan` sono metadati STA del namespace SDK `nvs.net80211`. Le variazioni osservate nel checkpoint `9ff4124` sono coerenti con l'associazione automatica all'AP sul canale 1.
- Il profilo `RADAR_UI_TRIAL=1` disabilita la persistenza Arduino e la riconnessione prima di avviare STA, verifica `WIFI_STORAGE_RAM` e resta offline per tutta la prova. In Arduino ESP32 3.3.12, `WiFi.persistent(false)` imposta il criterio per la successiva inizializzazione, quindi l'ordine precede `WiFi.mode(WIFI_STA)`. Il profilo standard conserva il portale, il salvataggio esplicito e le transizioni di connessione.
- Il peer ESP-NOW usa il canale letto dal record radio validato. Per il banco DRC1 corrente è 6. La prova imposta e rilegge il canale prima di registrare il peer e richiede che STA sia disassociata. Il loader apre i record in sola lettura. L'ultimo dump verificato conserva DRC1 identico e DRC2 assente. Il guard contro la cancellazione integrale NVS resta attivo nel candidato.
- **PASS host** profili standard e Trial, conservazione credenziali simulate, canale 6 e un secondo canale parametrico, rifiuto del cambio con AP associato, rilettura canale errata e fallimento della selezione RAM. Il test standard copre salvataggio esplicito, connessione, scollegamento e retry. Passati ASan/UBSan sul profilo Trial e il test regressione della callback beacon.
- **PASS build e verifica** `RADAR_UI_TRIAL=1 scripts/build_linux.sh <directory-nuova>` con Arduino ESP32 3.3.12. Verificati formato, checksum, hash d'immagine, tabella rispetto al backup e chiamate effettive nell'ELF per storage RAM, canale e guard NVS. Candidato da 1.772.704 byte, SHA-256 `717ff2da669e39b88c15faf17a5c5df8d53f68122cc760fdc2a3e123405652b4`, OTA1 `0x310000`, settori interessati fino a `0x4c0fff`.
- **NOT RUN** flash, reset, prova DRA1 60 s e confronto della NVS post-avvio sul nuovo candidato. Il confronto fisico deve confermare DRC1 identico, assenza di DRC2 e sole eventuali differenze PHY.

### M2a — prova fisica del candidato offline

- **PASS programmazione e readback** COM11 confermata in ROM. Copia corrente integrale di OTA1 da 3 MiB acquisita e confrontata con la ricostruzione dal readback precedente. Scritto solo il candidato da 1.772.704 byte in OTA1 a `0x310000`, con ultimo settore fino a `0x4c0fff`. Rilettura identica byte per byte e SHA-256 confermato.
- **PASS avvio e radio offline** console stabile per 70 s, canale 6, STA senza AP, zero connessioni o disconnessioni STA. Il firmware riporta AMOLED e controller touch inizializzati, batteria scollegata e motore SafeOff. L'operatore ha confermato il funzionamento di menu e animazioni e ha modificato la luminosità dalla UI durante la prova.
- **BLOCCATO DRA1 Paired e recupero** DRC1 caricato, PMK e peer inizializzati, `aligned=1 state=2 confirmed=0`. Il radar ha registrato 133 invii con consegna fallita e zero callback DRA1 RX. Nessuna telemetria applicativa dalla lettura passiva del C6. Il collegamento richiede il C6 operativo sul canale 6. Il C6 non è stato riavviato né programmato.
- **PASS NVS post-avvio** due letture ROM identiche. DRC1 coincide con il backup, DRC2 assente e metadati STA invariati rispetto a prima del flash. Le differenze logiche sono cinque record PHY consentiti e `radar_os/brightness`, modificato intenzionalmente dall'operatore durante la prova.
- **PASS integrità** bootloader, tabella, OTA0, `otadata` e immagine OTA1 verificati dopo l'avvio. Fine prova con S3 in ROM, senza ulteriori avvii o rollback. Backup e log conservati fuori da Git.
- **Diagnosi C6 in sola lettura** COM12 corrisponde al C6 del banco. Nessuna telemetria applicativa e risposta positiva dalla ROM in modalità download, senza stub RAM. Canale, stato ESP-NOW e contatori correnti saranno verificabili quando l'app M1d sarà in esecuzione. Il riferimento M1d è 60 s Paired sul 6, conferme S3 21 → 41 e C6 66 → 86.

### M2a — prova DRA1 conclusiva

- **PASS DRA1** dopo il ricollegamento USB del C6 senza BOOT, COM12 ha restituito telemetria applicativa M1d con DRC1. S3 UI Trial e C6 M1d Paired sul canale 6 per 60 s. Conferme S3 1 → 20 su 24 campioni e C6 2 → 22 su 13 campioni nell'acquisizione passiva. Zero nuovi scarti RX, rifiuti di autenticazione, retry o stalli TX. Il radar era in STA senza AP.
- **PASS avvio S3** inizializzazione AMOLED e touch confermata via seriale. La verifica NVS e l'uso di menu, animazioni e luminosità fanno riferimento alla prova precedente dello stesso candidato.
- **NOT RUN** perdita e recupero provocati nel campione M2a. Il recupero rimane documentato nel collaudo M1d. Entrambi i dispositivi sono rimasti in esecuzione al termine dell'acquisizione.

## LED USER DRA1 della sfera XIAO ESP32-C6

- Il LED USER è su GPIO15, attivo LOW. Il firmware carica HIGH prima di abilitare l'uscita e configura il pad come ingresso con pull-up durante il light sleep. La sfera corrente resta nel consueto ciclo radio. GPIO3 e GPIO14 servono lo switch antenna e USB usa GPIO12/13. GPIO15 viene configurato in `setup()`, dopo il campionamento degli strap. [Pinout Seeed](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/) · [Schema Seeed](https://files.seeedstudio.com/wiki/SeeedStudio-XIAO-ESP32C6/XIAO_ESP32_C6_v1.0_SCH_260114.pdf) · [GPIO ESP32-C6](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32c6/api-reference/peripherals/gpio.html)
- Il loop visualizza lo stato DRA1 autenticato con 1 Hz in attesa, acceso fisso in Paired e 4 Hz in Lost. Un disallineamento di canale usa 4 Hz dopo una sessione e 1 Hz in attesa della prima. In caso di inizializzazione fallita il LED resta spento. La logica usa `millis()` e scrive GPIO15 solo quando cambia il livello. Il profilo DEV conserva il suo comportamento.
- **PASS host** `g++ -std=c++11 -Wall -Wextra -Werror -Iinclude -Iexamples/dragon_ball_beacon tests/dra1_user_led_test.cpp`. Verificati periodi, transizioni, recupero, iterazioni saltate e overflow di `millis()`. Suite DRA1 host passata.
- **PASS build C6 `no_ota`** Arduino ESP32 3.3.12. Profilo radio di banco compilato con `scripts/build_m1c_c6.sh radio <directory-nuova>`, ID 1, canale 6, antenna U.FL e guard NVS. Anche il profilo DEV è stato compilato. Verificati target ESP32-C6, checksum, hash d'immagine, tabella compatibile con il backup e separazione dei profili. Il candidato radio è da 965.360 byte con SHA-256 `0664809c7553487947b7ff330a55c6cf8be480881c95998984b2cc83b23ce07e`. Il singolo slot app0 parte da `0x10000`, con settori interessati fino a `0xfbfff`.
- **PASS installazione autorizzata** C6 COM12 verificato in ROM, backup corrente completo da 4 MiB e layout `no_ota` confermati. Scritti 965.360 byte della sola app a `0x10000`. Readback identico per byte e SHA-256. Prima del riavvio tutte le regioni fuori dai settori programmati, inclusi NVS e otadata, coincidevano con il backup.
- **PASS avvio e stati DRA1 via seriale** il C6 ha caricato DRC1, inizializzato PMK e peer e raggiunto Paired sul canale 6. Conferme 2 → 8, poi transizione a Lost nel campione a circa 40 s. L'operatore ha confermato di aver messo volontariamente il radar in standby durante la prova. L'evento viene registrato come perdita indotta, senza attribuirlo a una regressione radio. Zero scarti RX, rifiuti di autenticazione e stalli TX.
- **PASS recupero dopo il risveglio** entrambe le schede sono tornate Paired sul canale 6 senza reset, provisioning o cambi firmware. Nel campione di 60 s le conferme sono cresciute da 11 a 31 sul radar e da 17 a 37 sul C6. Callback RX S3 34 → 54 e C6 35 → 55. Nessun incremento di retry, duplicati, scarti, rifiuti di autenticazione o errori di consegna e nessuno stallo TX durante il campione.
- **NOT RUN verifica visiva** di frequenze, LED fisso e spegnimento. Entrambe le schede sono rimaste in esecuzione. Log e backup restano privati.

## M2b — ricerca RSSI autenticata in modalità Chiuso

- **Implementata** la misura RSSI dalla metadata ESP-NOW dell'ACK DRA1 che chiude un probe pendente della sessione corrente. Valori mancanti o scaduti, broadcast DRB2, ACK duplicati, vecchie sessioni e tag errati sono esclusi. Formato DRA1, chiavi, associazioni e retry seguono il percorso esistente. Nessuna nuova libreria.
- Il filtro usa mediana su cinque campioni, smoothing esponenziale, isteresi di 4 dB per qualità e due campioni coerenti per la tendenza. Le soglie iniziali sono `-82` e `-60 dBm`, con scadenza dopo 10 s. I parametri si modificano in `ProximityConfig`. Standby, perdita e nuova sessione invalidano la misura e azzerano il filtro per il campione successivo. Dopo il risveglio serve un nuovo ACK. Debug può mostrare l'ultimo valore con l'indicazione di scadenza.
- In **Impostazioni → Radar → Reale** la schermata Chiuso usa tre anelli concentrici per evidenziare la qualità RSSI. Mostra attesa, segnale debole, tendenza di avvicinamento o allontanamento, vicinanza elevata e stato trovato. L'operatore conferma il ritrovamento dal touch e può avviare una nuova ricerca. In **Debug → Wi-Fi e sfere** e con `beacons` si leggono RSSI grezzo e filtrato, qualità ed età. Un segnale forte indica vicinanza relativa. Chiuso usa soltanto ESP-NOW.
- **PASS host** 10 esecuzioni con `-Wall -Wextra -Werror`. Simulate salita, discesa, oscillazioni, soglie, scadenza, recupero, standby, overflow del clock e conferma manuale. Test DRA1 verificano la selezione dei soli ACK validi e regressioni su retry e fixture. Passati ASan/UBSan per prossimità e DRA1, oltre a beacon RX, standby, gesture, launcher e navigazione impostazioni.
- **PASS build S3** UI Trial e regressione banco M1c con Arduino ESP32 3.3.12. Verificati target ESP32-S3, integrità dell'immagine, separazione da DEV e compatibilità con la tabella partizioni S3 già letta. Il candidato UI Trial misura 1.775.760 byte, SHA-256 `683c4c316fd7a0e6427c1a7029ef7810efa3dc96cdca4960352dd1929b226a76`. L'immagine rientra in OTA1 a `0x310000` e occupa i settori fino a `0x4c1fff`. Binario e log conservati fuori da Git.
- **Baseline fisica conservata** S3 UI Trial M2a SHA-256 `717ff2da669e39b88c15faf17a5c5df8d53f68122cc760fdc2a3e123405652b4`. C6 con LED USER SHA-256 `0664809c7553487947b7ff330a55c6cf8be480881c95998984b2cc83b23ce07e`.
- **NOT RUN collaudo fisico** dopo autorizzazione riconfermare S3 e OTA1 in ROM e verificare le copie correnti. Programmare la sola app S3 e verificarla in rilettura, conservando OTA0, otadata e impostazioni. Usare il C6 già installato. Avviare Chiuso sul canale 6 senza AP. Con alimentazione USB e attuatori isolati, osservare avvicinamento e allontanamento, perdita temporanea e standby. Controllare in Debug valori grezzo e filtrato, età e qualità, poi confermare manualmente il ritrovamento. La calibrazione delle soglie richiede prove nell'ambiente reale.

### M2b — primo tentativo di preflight hardware

- **PASS offline** candidato da 1.775.760 byte e SHA-256 autorizzato confermati. La baseline M2a coincide per SHA-256 con il readback conservato. Verificati le due copie integrali originali da 16 MiB, identiche byte per byte, e il manifest delle catture M2a. Sono riferimenti storici per il prossimo confronto live.
- **PASS identificazione USB** COM11 coincide con l'identità USB storica del radar. Il tentativo mirato ha utilizzato solo COM11.
- **FAIL accesso ROM** il tentativo esptool senza reset ha restituito `No serial data received`. Modalità corrente e stabilità del trasporto in ROM da verificare. Serve confermare fisicamente alimentazione solo USB e isolamento di batteria e attuatori prima di entrare in ROM.
- **NOT RUN** verifica live di tabella partizioni, selezione OTA e backup corrente, scrittura e readback, avvio di UI e launcher, DRA1 Paired per 60 s sul canale 6 senza AP, RSSI, standby e confronto NVS. Touch e prove di movimento attendono osservazione dell'operatore. Nessuna scrittura, reset o rollback richiesto. Il C6 è rimasto intatto. Log privato.

### M2b — installazione e collaudo automatico S3

- **PASS preflight live** dopo ingresso manuale in ROM, radar riconfermato su COM11, ESP32-S3 con flash da 16 MiB, Secure Boot e Flash Encryption disabilitati. Due letture identiche di intestazione flash, NVS, otadata e intero slot OTA1 da 3 MiB. OTA0, bootloader e tabella coincidono con l'originale. OTA1 coincideva con la baseline M2a ed era già selezionato, sequenza 4 VALID. Banco alimentato via USB con batteria e attuatori scollegati secondo la configurazione già documentata. Il PMIC all'avvio ha confermato batteria assente e la diagnostica GPIO riportava SafeOff.
- **PASS installazione e readback** scritti in OTA1 a `0x310000` solo i 1.775.760 byte del candidato autorizzato. Settori interessati fino a `0x4c1fff`. Rilettura integrale prima dell'avvio identica per byte, SHA-256 `683c4c316fd7a0e6427c1a7029ef7810efa3dc96cdca4960352dd1929b226a76`. NVS, otadata, OTA0, bootloader, tabella e coda OTA1 verificati prima dell'avvio.
- **PASS avvio e DRA1 offline** console risponde, inizializzazione AMOLED CO5300 e touch CST9217 riuscita. Canale 6, STA senza AP, 31 campioni Paired in 60,70 s. Conferme 12 → 22 e callback RX 59 → 105. Retry accodati 32 → 58. Zero rifiuti di autenticazione, drop RX, errori di consegna e stalli TX.
- **PASS diagnostica RSSI su USB** grezzo da −26 a −24 dBm, filtrato da −25,2 a −24,1 dBm, età da 163 a 9.510 ms e qualità 3 in tutti i 31 campioni Paired. Al primo avvio la misura risultava indisponibile prima del primo ACK. Il campione seriale non include una scadenza a 10 s o una transizione di standby.
- **PASS integrità dopo il campione** doppia lettura NVS identica byte per byte al preflight. DRC1 identico, DRC2 assente, metadati Wi-Fi invariati e nessuna variazione PHY osservata. OTA0, bootloader, tabella, coda OTA1 e otadata invariati. OTA1 sequenza 4 VALID. Dopo le letture ROM, riavvio normale e risposta dell'app M2b confermata. C6 intatto, nessun rollback. Backup e log privati.
- **PASS qualitativo, taratura aperta** l'operatore ha confermato che grafica, avvicinamento e allontanamento, conferma touch e recupero dopo standby hanno funzionato abbastanza bene. È richiesta più sensibilità a breve distanza. La scadenza a 10 s resta verificata nei test host. Il campione live non l'ha isolata.

- **PASS controllo seriale successivo** Paired sul canale 6 senza AP, conferme 55 → 56, zero nuovi retry, drop RX, rifiuti di autenticazione o stalli TX nel breve campione. RSSI valido, grezzo da −20 a −19 dBm, filtrato da −19,3 a −19,2 dBm. Il campione documenta lo stato successivo alla prova e non contiene la transizione di standby.

- **Ipotesi per la taratura** il livello forte resta invariato sopra la soglia di −60 dBm, anche quando l'RSSI continua a variare. Raccogliere misure con l'antenna attuale e verificare prima la risoluzione della grafica e la risposta del filtro. Lo XIAO può commutare tra antenna ceramica e U.FL tramite GPIO14. Un confronto A/B deve mostrare che l'antenna interna migliora la separazione dei valori a breve distanza senza degradare il collegamento. Un eventuale cambio automatico richiede isteresi e riavvio del filtro RSSI, perché la commutazione stessa modifica il riferimento. [Documentazione Seeed](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)


## M2b.1 — più risoluzione grafica a breve distanza

- M2b associava direttamente qualità debole, media e forte agli anelli. Sopra −60 dBm la grafica restava al massimo anche con valori misurati intorno a −19 dBm.
- M2b.1 converte il valore filtrato in 15 livelli grafici su tre anelli, nominalmente da −90 a −15 dBm. Le soglie distano da 4 a 7 dB, con gradini più stretti vicino alle misure del banco. L'isteresi grafica di 2,5 dB impedisce passaggi ripetuti tra due livelli per oscillazioni di 1–2 dB. Il filtro mediano ed esponenziale, le soglie della qualità, la tendenza e la scadenza a 10 s sono quelli di M2b.
- L'intensità determina lo spessore degli anelli, la tendenza compare nel messaggio centrale e il collegamento DRA1 su una riga separata. Il ritrovamento resta manuale. In Debug e su `beacons` sono presenti grezzo, filtrato, qualità, età e livello. Il livello 0 indica misura indisponibile.
- **PASS host** 10 esecuzioni con `-Wall -Wextra -Werror` e ASan/UBSan per prossimità e DRA1. I valori −85, −65, −60, −45, −30, −24 e −19 dBm hanno prodotto i livelli 1, 4, 5, 8, 11, 13 e 14. Verificati i limiti della scala, le oscillazioni ripetute di 2 dB intorno a una soglia, la scadenza, l'invalidazione e il recupero.
- **PASS build S3 UI Trial** Arduino ESP32 3.3.12, `RADAR_UI_TRIAL=1 RADIO_DEV_INSECURE=0 scripts/build_linux.sh`. Verificati ESP32-S3, checksum, hash interno, corrispondenza dei sorgenti di staging e tabella partizioni rispetto alla lettura ROM. App da 1.775.984 byte, SHA-256 `8752e14abd7d229edf5e08f068b3097a743d33eb26826fb9aab70fb6143360a9`. OTA1 a `0x310000`, settori fino a `0x4c1fff`. Binario e log conservati fuori da Git.
- **NOT RUN confronto fisico** raccogliere serie nelle stesse posizioni e con gli stessi orientamenti, annotando RSSI grezzo e filtrato, età, livello e retry. Confrontare gli anelli a breve distanza e verificarne la stabilità a radar fermo. Antenna e protocollo radio non richiedono modifiche per questa prova.


## M3a — vibrazione sfera C6 e predisposizione WS2812B

- **Pin verificati offline.** D4/GPIO22 è libero nello sketch e assegnato al segnale Grove (giallo). Il modulo con driver integrato usa 3V3 (rosso) e GND (nero), con filo bianco scollegato. D4/D5 sono anche SDA/SCL e restano riservati agli attuatori sul profilo sfera. GPIO15 serve al LED USER, GPIO3/14 all'antenna. Seeed documenta l'ingresso attivo HIGH e il campo di alimentazione del modulo da 3,0 a 5,5 V. Lo schema Grove v1.2 mostra un pull-down da 100 kΩ sulla base del transistor. Verificare la revisione installata, il livello a reset, la corrente di spunto e la tenuta della rail 3V3 al banco. [Pinout XIAO C6](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/) · [Modulo Grove](https://wiki.seeedstudio.com/Grove-Vibration_Motor/)
- **Motorino.** Nel profilo radio autenticato ID 1 con USB CDC, D4 viene predisposto LOW prima dell'inizializzazione NVS nel profilo di banco. Dopo l'inizializzazione riuscita del peer sicuro e del driver, l'avvio genera un impulso di 60 ms. `motor_test` a 115200 baud con fine riga genera 120 ms via USB. Il RMT emette un impulso singolo con uscita finale LOW indipendente dalla console e la temporizzazione con `millis()` chiude il ciclo entro il limite software di 250 ms quando il loop avanza. Il cooldown di almeno 3 s vale anche dopo l'impulso di avvio. Pairing, perdita del collegamento e pacchetti ESP-NOW non comandano il motore. L'inizializzazione fallita mantiene disabilitato il test. [RMT ESP32-C6](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32c6/api-reference/peripherals/rmt.html)
- **Predisposizione RGB.** `sphere_rgb.h` riserva D5/GPIO23 e descrive tre pixel disabilitati. Il firmware lascia il GPIO senza configurazione RGB. Per il futuro cablaggio, D5 arriva al DIN del primo LED, il DOUT di ciascun LED va al DIN del successivo e VDD/GND sono distribuiti ai tre LED con massa comune al C6. Dimensionare l'alimentazione sul modello reale e sulla corrente totale. Verificare la soglia VIH della revisione a VDD scelto. Una revisione che richiede 0,7 × VDD a 5 V vuole almeno 3,5 V sul DIN e richiede adattamento di livello dal C6. Alimentazione da 14500 da validare separatamente. [Specifiche Worldsemi](https://www.world-semi.com/web/userfiles/productfile/WS2812B_V5WDatasheet_V6.1_EN.pdf) · [Differenze tra revisioni](https://www.world-semi.com/web/index.php?classid=22&id=587&lanstr=en&topclassid=19)
- **PASS host** cinque esecuzioni, con `-Wall -Wextra -Werror` per driver simulato, DRA1 e LED USER e ASan/UBSan per driver e DRA1. Coperti LOW iniziale, impulsi da 60 e 120 ms, assenza di retrigger, limite software, cooldown, errori, wrap di `millis()` e righe seriali malformate.
- **PASS build C6** radio e DEV con Arduino ESP32 3.3.12, `PartitionScheme=no_ota,CDCOnBoot=cdc`. Verificati target, checksum, hash, tabella compatibile con il backup e presenza del driver RMT e dei comandi solo nel profilo radio. Candidato radio da 978.720 byte, SHA-256 `eceb231ffb197efc2b16d0b8a7c978552fa6124af9bf3b01cb54945d8e4551a5`. App a `0x10000`, settori fino a `0xfefff`. Binario, baseline e log conservati fuori da Git.
- **NOT RUN banco** collaudo motorino, soglia termica, durata elettrica, inattività di D4 in ROM/reset, LED e stabilità DRA1 durante gli impulsi. Il software assume il controllo del pad dopo il boot ROM. Nessun flash o accesso hardware.
- **Procedura dopo autorizzazione.** Usare solo USB e tenere batteria e RGB scollegati. A banco spento controllare cablaggio, continuità e assenza di corti. Misurare D4 nel reset/ROM con ingresso Grove isolato se necessario, e fermarsi se il livello inattivo o l'alimentazione non sono sicuri. Identificare il C6 in ROM, acquisire due letture correnti identiche e verificare layout `no_ota` e hash. Scrivere solo l'app a `0x10000`, confrontarla integralmente in rilettura e preservare bootloader, tabella, otadata e NVS. Conservare l'app precedente per un eventuale ripristino autorizzato.
- **Criteri del collaudo.** All'avvio attendere un solo impulso breve e verificare DRA1 Paired e LED USER fisso. Inviare `motor_test`, ripeterlo entro 3 s e verificare che la ripetizione venga rifiutata. Chiudere la console subito dopo un comando accettato e verificare che il segnale torni LOW. Confrontare i contatori DRA1 del C6 e del radar prima, durante e dopo la prova. Interrompere se si osservano surriscaldamento, livello HIGH prolungato, caduta della 3V3 o reset.

### M3a — preflight hardware in attesa del controllo elettrico

- **PASS offline** candidato radio da 978.720 byte, SHA-256 `eceb231ffb197efc2b16d0b8a7c978552fa6124af9bf3b01cb54945d8e4551a5`, verificato per ESP32-C6 e layout storico `no_ota`. Destinazione autorizzata app0 a `0x10000`.
- **PASS enumerazione USB** COM12 presente. Porta rilevata tramite inventario del sistema, senza aprire la seriale. Identità del chip e modalità ROM da verificare dopo il controllo elettrico.
- **PENDING misura operatore** confermare alimentazione solo USB con batteria e RGB scollegati, cablaggio Grove D4/GPIO22 → giallo, 3V3 → rosso, GND → nero e bianco isolato. Controllare assenza di corto 3V3–GND a scheda spenta e misurare D4 rispetto a GND in inattività e in ROM. Il risultato deve essere LOW.
- **NOT RUN** identificazione ROM, doppio backup live, flash e readback. Restano da collaudare l'impulso intenzionale di avvio da 60 ms, il comando USB, il cooldown, il LED USER e DRA1 con il motore in funzione. Nessun reset o scrittura.

### M3a — preflight interrotto per vibrazione inattesa

- **PASS** dopo la conferma elettrica dell'operatore, COM12 identificata come XIAO ESP32-C6 in ROM. Flash da 4 MiB. Il primo dump corrente contiene ancora l'app C6 con LED USER del collaudo precedente.
- **STOP** l'operatore ha segnalato vibrazione mentre era in corso la lettura flash con stub in RAM, prima della fase di scrittura. Interrotto il processo. Conservati un dump completo da 4 MiB e una seconda lettura parziale da 3.670.016 byte. Tutti i byte della seconda lettura coincidono con la prima, ma manca il secondo backup integrale.
- **NOT RUN** flash, readback e avvio M3a. L'impulso di avvio da 60 ms del candidato non può spiegare l'evento perché M3a non è stato installato. Nessuna scrittura o ripristino eseguito. Log e dump conservati in locale.
- **Riscontro operatore** vibrazione continua durante l'evento, assente dopo il ricollegamento USB. COM12 riconfermata dall'inventario USB. Il livello D4 durante l'evento non è stato misurato. La causa resta aperta. Prima di un altro ingresso in ROM, togliere alimentazione e scollegare completamente il modulo Grove. Riprendere backup e verifica ROM a motore isolato. Ricollegare il Grove a scheda spenta soltanto dopo aver verificato il comportamento di D4 durante reset e ROM.

### M3a — backup ROM con motorino isolato

- **PASS** dopo lo scollegamento del Grove confermato dall'operatore, XIAO ESP32-C6 su COM12 in ROM con flash da 4 MiB. Due letture indipendenti integrali identiche, con checksum di trasporto e SHA-256 verificati. Backup conservati fuori da Git.
- **PASS** layout `no_ota`, app0 a `0x10000` da 2 MiB e firmware LED USER precedente confermati. Confronto con la cattura prima dell'interruzione identico fuori dalla NVS. In NVS si osservano quattro variazioni logiche PHY; DRC1 invariato e DRC2 assente.
- **PENDING misura operatore** C6 lasciato in ROM con stub in RAM, Grove scollegato. Misurare D4/GPIO22 rispetto a GND nello stato attuale. Rimane da verificare il transitorio di reset prima di ricollegare il modulo. **NOT RUN** scrittura, readback M3a e collaudo del motore.

### M3a — FAIL livello inattivo su D4

- L'operatore ha misurato **3,26 V tra D4/GPIO22 e GND**, con Grove scollegato e C6 in ROM dopo il caricamento dello stub in RAM. Il livello è HIGH e il Grove è attivo HIGH. La misura a vuoto non distingue una polarizzazione debole da un'uscita pilotata e non identifica da sola la causa della vibrazione. Il datasheet ESP32-C6FH4 documenta un pull-up debole al reset sulla funzione SDIO_DATA2/GPIO22. Il firmware applicativo può impostare LOW solo dopo il boot ROM. [Datasheet Espressif, tabella pin](https://documentation.espressif.com/esp32-c6_datasheet_en.html?q=datasheet) · [Grove Seeed](https://wiki.seeedstudio.com/Grove-Vibration_Motor/)
- Backup verificati. **NOT RUN flash e collaudo motore.** Tenere il Grove scollegato da D4. D2/GPIO2 è un candidato da verificare, dato che il datasheet lo riporta come ingresso senza pull-up predefinito al reset. Prima di un'eventuale modifica, misurare il pin a modulo isolato durante reset, ROM e lettura tramite stub. Il cablaggio e il candidato autorizzato restano quelli originali.

### M3a — nuove misure su D4 e verifica da completare

- **Misure operatore** D4 a 3,26 V inizialmente e a 0,60 V in seguito con Grove scollegato, senza reset o riprogrammazione riferiti fra le misure. Collegando temporaneamente il Grove il motore è rimasto fermo. D2 ha misurato 0,50 V. Misure con multimetro, prova con 10 kΩ ancora da eseguire. Si conserva D4/GPIO22.
- **Analisi limitata allo stato elettrico** una lettura a vuoto può corrispondere a pull-up debole, pad in alta impedenza o livello pilotato. Espressif indica per GPIO22 un pull-up al reset e 45 kΩ tipici per il pull-up interno. Il Grove v1.2 ha SIG collegato alla base del transistor tramite 1 kΩ e un pull-down da 100 kΩ sulla base. La base del transistor è un carico non lineare. L'assenza di vibrazione osservata dopo il collegamento non dimostra che il pull-down basti durante accensione, ROM e flashing. [Datasheet Espressif](https://documentation.espressif.com/esp32-c6_datasheet_en.html?q=datasheet) · [Schema Grove v1.2](https://wiki.seeedstudio.com/Grove-Vibration_Motor/#resources)
- **NOT RUN lettura registri** COM12 associata al C6. La sincronizzazione con lo stub precedente, tentata senza reset, non ha ricevuto risposta. Lo stato attuale di mux, pull e abilitazione dell'uscita rimane indeterminato. Anche eventuali letture di questi registri sarebbero istantanee. I backup precedenti da 4 MiB sono stati riverificati per SHA-256.
- **Prossimo controllo** con Grove, batteria e LED RGB scollegati, confrontare D4–GND a vuoto e con 10 kΩ tra D4 e GND nello stesso stato. Installare la resistenza a scheda disalimentata nella prossima prova autorizzata, evitando nuovi reset in questa fase. Un forte calo sotto carico indica polarizzazione debole o alta impedenza. Un livello che resta vicino a 3,3 V richiede invece di indagare una sorgente più forte. Con un pull-up da 45 kΩ e un pull-down esterno da 10 kΩ, il partitore darebbe circa 0,60 V tipici a 3,3 V. La prova con resistenza non garantisce il rispetto della soglia né l'assenza di impulsi. Prima di ricollegare il Grove, verificare con oscilloscopio l'accensione, l'ingresso in ROM e l'attività dello stub con il carico scelto. Se il pad viene pilotato HIGH, occorre una soluzione che inibisca il motore fuori dall'app.
- **Vincoli invariati** candidato con impulso di avvio da 60 ms e `motor_test` da 120 ms. Nessuna modifica a DRA1, LED USER, antenna o RGB. Flash, nuovo reset, attivazione del motorino e rollback attendono autorizzazione.
- **Ricontrollo 2026-11-24** COM12 identificata via USB. Nessun dato in ascolto passivo e nessuna risposta alla sincronizzazione senza reset. Stato di D4 via registri ancora indeterminato. Entrambi i backup da 4 MiB passano di nuovo la verifica SHA-256. Attesa della misura con 10 kΩ a Grove scollegato prima di riprendere il collaudo.
- **Nuovo riscontro operatore** D4 ha misurato 0,37 V a vuoto. L'operatore non dispone al momento della resistenza da 10 kΩ e ha montato motorino e batteria. Stato di alimentazione e protezione della batteria non confermati. Nessuna verifica del transitorio e nessun nuovo comando al C6. Per riprendere il preflight, ripristinare a scheda spenta la configurazione solo USB con batteria scollegata e Grove isolato. La prova statica con resistenza può essere rinviata per un eventuale collaudo USB con attuatore isolato. Rimane aperta la verifica elettrica prima di alimentare il motore. Reset, flash e prova di vibrazione richiedono nuova autorizzazione.

### M3a — installazione e avvio con Grove isolato

- **PASS preflight** conferma dell'operatore di alimentazione solo USB con batteria e Grove fisicamente scollegati. C6 identificato in ROM su COM12. Riverificato il candidato M3a autorizzato e acquisiti due nuovi backup completi da 4 MiB identici. Confermati flash da 4 MiB, layout `no_ota`, firmware precedente con LED USER, DRC1 e assenza DRC2.
- **PASS scrittura e readback** scritti 978.720 byte della sola app0 a `0x10000`. Ultimo settore `0xfefff`. Readback identico byte per byte e SHA-256 `eceb231ffb197efc2b16d0b8a7c978552fa6124af9bf3b01cb54945d8e4551a5`. La lettura integrale prima dell'avvio conferma tutti i byte fuori dai settori programmati invariati, inclusi NVS, otadata, bootloader e tabella. Copie e log restano privati.
- **PASS avvio senza carico** una sola riga seriale di accodamento dell'impulso da 60 ms. DRA1 Paired sul canale 6 per 10,02 s osservati, conferme 2 → 5 e zero drop RX, rifiuti HMAC, stalli TX o errori di consegna. Radar S3 e antenna intatti.
- **NOT RUN prova elettrica e motore** durata sul pin, transitori di GPIO22 a reset e in ROM, risposta meccanica, LED USER a vista e comando `motor_test` sotto carico. Prossimo passo con operatore presente, spegnere il C6, ricollegare solo il Grove e riaccendere via USB con batteria e RGB scollegati. Interrompere l'alimentazione se la vibrazione è continua o si osservano anomalie.

### M3a — collaudo supervisionato USB con Grove collegato

- **PASS fisico riferito dall'operatore** Grove ricollegato a scheda spenta e C6 rialimentato solo via USB. All'accensione un impulso breve seguito dallo spegnimento del motorino. Al comando diagnostico l'operatore ha confermato un solo impulso breve e lo spegnimento successivo.
- **PASS via seriale** su COM12 il primo comando ha risposto `MOTOR_TEST STARTED 120 ms`. Il secondo, inviato a 453 ms, ha risposto `MOTOR_TEST WAIT`. Il test sotto carico ha verificato il rifiuto entro cooldown. La riabilitazione al limite dei 3 s resta coperta dal test host.
- **PASS DRA1 durante la prova** Paired durante 60,03 s dal comando, con 12 campioni successivi e conferme 12 → 32. Callback RX 43 → 63 e consegne TX 31 → 51. Nessun incremento di retry, duplicati, drop RX, rifiuti di autenticazione o errori di consegna. Nessun reset, brownout o stallo TX osservato nella telemetria. C6 sul canale 6 già verificato al boot M3a.
- **NOT RUN misure strumentali** durata elettrica esatta degli impulsi, transitori D4 durante reset e ROM e tenuta della 3V3 sotto carico. Indicazione del LED USER non osservata in questo campione. Firmware S3 e C6 e associazione invariati durante il test. Nessun flash o reset richiesto. Log privati.


## M3b — vibrazione remota manuale via DRA1

- **M3a PASS funzionale via USB** confermati gli impulsi di avvio e test USB e il cooldown osservato. Restano aperte le misure strumentali di durata, D4 durante reset e ROM e tenuta della 3V3.
- Il radar richiede un impulso da 120 ms solo dal tasto **Debug → Vibrazione sfera → Fai vibrare la sfera**. La sfera usa lo stesso driver RMT su GPIO22 e lo stesso cooldown della prova USB. L'impulso di avvio da 60 ms resta disponibile. Ricerca RSSI, LED USER, antenna, pairing, alimentazione e predisposizione RGB disabilitata conservati.
- Il risultato applicativo è autenticato e correlato al comando. La sfera riusa il risultato dell'ultima richiesta nei retry e scarta richieste precedenti o di altre sessioni. Il radar distingue accettato, cooldown, indisponibile ed esito non confermato. L'operatore verifica la vibrazione fisica. Nessuna misura RSSI deriva da un comando o dal risultato.
- **PASS host** 12 esecuzioni, con `-Wall -Wextra -Werror` e ASan/UBSan per DRA1 e prossimità. Coperti pulsante e navigazione, HMAC e peer errati, coda RX scaduta, perdita della risposta, duplicati e replay, ACK di probe separato, risultati appena ricevuti o in ritardo, timeout, cooldown e regressioni LED USER e M2b.1.
- **PASS build** S3 UI Trial offline e C6 radio `no_ota` con Arduino ESP32 3.3.12. Verificati target, checksum, hash interno, marcatori, sorgenti di staging e tabelle corrispondenti a quelle lette in ROM. Binari, ELF, log e manifest SHA-256 fuori da Git.
  - S3, 1.777.600 byte, SHA-256 `4bdff1f8837deba005d1164dd86b5e0e8e72350e715fb2a6731c5e2210c88653`, OTA1 a `0x310000`, ultimo settore fino a `0x4c1fff`.
  - C6, 979.584 byte, SHA-256 `856babf41f4762ec0e2c4eb2b0b5a93624bf9e383788f7d12eacb1072cdbe425`, app0 a `0x10000`, ultimo settore fino a `0xfffff`.
- **Baseline conservate** app S3 M2b installata (SHA-256 `683c4c316fd7a0e6427c1a7029ef7810efa3dc96cdca4960352dd1929b226a76`), candidato S3 M2b.1 (`8752e14abd7d229edf5e08f068b3097a743d33eb26826fb9aab70fb6143360a9`) e app C6 M3a installata (`eceb231ffb197efc2b16d0b8a7c978552fa6124af9bf3b01cb54945d8e4551a5`).
- **NOT RUN banco M3b** nessun flash. Serve un'installazione autorizzata di entrambe le app. La taratura fisica M2b.1 è rinviata.

### M3b — installazione e radio con attuatori isolati

- **PASS preflight** operatore ha confermato entrambe le schede solo via USB, con batterie e attuatori fisicamente scollegati. S3 in ROM su COM11 e C6 in ROM su COM12. Verificati hash, layout e DRC1. Due letture indipendenti coincidenti per area di avvio, NVS, otadata e OTA1 dello S3. OTA0 ha lo stesso digest della copia originale. Due dump integrali indipendenti da 4 MiB, identici, per il C6.
- **PASS installazione sequenziale** S3, 1.777.600 byte a `0x310000`. C6, 979.584 byte a `0x10000`. Entrambe le immagini rilette e confrontate byte per byte prima dell'avvio. Verificati invariati NVS, otadata, bootloader e tabelle partizioni. Sullo S3 restano integri OTA0 e coda OTA1, con OTA1 selezionato come prima.
- **PASS avvio e radio** DRA1 Paired su entrambi per 60,03 s, canale 6 e S3 senza AP. Conferme S3 2 → 22 e C6 2 → 21. Zero nuovi drop, retry probe, rifiuti di autenticazione, errori di consegna o stalli TX. Inizializzazione AMOLED e controller touch riuscita. RSSI M2b.1 presente via seriale senza nuova taratura. Il comando motore risultava inattivo durante il campione.
- **PENDING prova motore supervisionata** prossimo passo C6 a scheda spenta, collegare il solo Grove e alimentare via USB. L'operatore verifica impulso di avvio, pulsante Debug, conferma, rifiuto entro cooldown e nuovo comando dopo attesa. Durate elettriche, transitori GPIO22 durante reset e ROM e tenuta della 3V3 restano da misurare. Log e backup in locale.

### M3b — prova touch in attesa del primo comando

- **PASS avvio motorino osservato** l'operatore ha confermato un impulso breve dopo il ricollegamento del Grove e l'alimentazione solo USB.
- **PASS stabilità dopo il ricollegamento** 120 s di monitoraggio seriale senza reset inattesi nella finestra, DRA1 Paired e conferme S3 30 → 69, C6 2 → 40. I contatori di retry ed errori di consegna già presenti al primo campione, dopo la riaccensione del C6, non sono cresciuti.
- **NOT RUN** prova del pulsante, esito del comando, cooldown e impulso successivo. Lo stato applicativo è rimasto inattivo durante la finestra. Il prossimo campione richiede i tocchi dell'operatore nella pagina 13/13 di Debug. Restano aperte le verifiche strumentali M3a.

### M3b — collaudo touch e conferme applicative

- **PASS via seriale** sulle due schede, senza flash o reset. Dal tasto **Debug 13/13 → Fai vibrare la sfera**, esiti applicativi DRA1 in sequenza accettato → cooldown → accettato. Il rifiuto è stato campionato 1,70 s dopo la prima conferma e la nuova accettazione 4,72 s dopo la prima.
- **PASS radio durante il comando** Paired sul canale 6 in tutta la finestra, almeno 10 s di monitoraggio dopo l'ultima conferma. Conferme S3 141 → 149 e C6 187 → 194. Nessun nuovo drop RX, errore di consegna, rifiuto di autenticazione, retry dei probe, duplicato o stallo TX. Nessun reset osservato.
- **PASS fisico riferito dall'operatore** primo e terzo tocco hanno prodotto un impulso ciascuno. Al secondo tocco il motore è rimasto fermo e il display ha mostrato l'invito a riprovare. Le due accettazioni sono verificate via seriale. Il testo positivo sul pannello non è stato confermato esplicitamente dall'operatore.
- **M3b PASS funzionale via USB** deduplicazione del comando con risposta persa verificata nei test host. Le misure strumentali M3a restano aperte. Log privati.


## M3c — feedback di gioco al ritrovamento

- **M3b PASS funzionale via USB.** Conservati i binari installati. Le misure strumentali M3a restano aperte.
- In **Radar → Reale**, un tocco sul pulsante di conferma con una misura attendibile e DRA1 Paired registra lo stato Trovata e produce una sola volta l'evento software `SphereFound` per quella ricerca. Il radar usa lo stesso comando autenticato di Debug per tentare l'impulso da 120 ms. Se la richiesta non parte o il C6 non la conferma, lo stato Trovata resta valido. `Nuova ricerca` azzera lo stato e attende una nuova misura.
- L'evento è riutilizzabile per le future animazioni RGB. Il gioco conserva il ritrovamento manuale e i LED esterni disabilitati. Driver, cooldown, formato DRA1, RSSI e pairing seguono i percorsi già collaudati. Il C6 resta quello M3b.
- **PASS host** 10 esecuzioni, comprese ASan/UBSan per prossimità e DRA1. Testate conferma con Paired e misura fresca, assenza della misura, misura scaduta, link indisponibile, conferma ripetuta, permanenza del ritrovamento dopo invalidazione e avvio di una nuova ricerca. Conservati i test M3b per retry e deduplicazione.
- **PASS build S3 UI Trial** Arduino ESP32 3.3.12. Target, checksum, hash interno, tabelle partizioni e confronto dei sorgenti di staging verificati. Candidato da 1.777.680 byte, SHA-256 `e6873cd926fd0e87b52bbb6b27e2ae6490747b86607d87794bbabcea28609f5e`, sola app OTA1 a `0x310000`, settori fino a `0x4c2fff`. Baseline S3 e C6 M3b riverificate per SHA-256. Artefatti e log fuori da Git.
- **NOT RUN collaudo fisico M3c.** Nessun flash.

### M3c — installazione e prima conferma

- **PASS preflight S3** COM11, alimentazione solo USB confermata dal PMIC nella configurazione banco, ESP32-S3 in ROM da 16 MiB, layout originale e DRC1 confermati. Due letture indipendenti coincidono per intestazione flash, NVS, otadata e intero slot OTA1. Verificata l'app M3b precedente e OTA0 originale.
- **PASS scrittura e readback** scritti 1.777.680 byte della sola app OTA1 a `0x310000` fino al settore `0x4c2fff`. Readback integrale identico prima dell'avvio. OTA0, NVS, otadata, bootloader, tabella e coda di OTA1 verificati invariati. Backup e log fuori da Git. C6 M3b senza flash o reset.
- **PASS via S3** AMOLED e controller touch inizializzati. DRA1 Paired per 60,19 s sul canale 6 senza AP, conferme 1 → 9, 27 retry di probe, zero drop, rifiuti di autenticazione o errori di consegna nella finestra. La seriale C6 è rimasta silenziosa durante il campione. Il peer rispondeva via DRA1 autenticato. Misure RSSI valide a campione, taratura M2b.1 rinviata.
- **Primo riscontro operatore** prima conferma funzionante. Il radar riportava esito applicativo accettato all'inizio del monitoraggio touch. La prova completa di nuova ricerca, tocchi ripetuti e comando manuale resta da registrare.
- **PASS conferma applicativa aggiuntiva** durante 144 s di monitoraggio S3 osservati stato iniziale accettato e un'altra transizione attesa → accettato, con DRA1 Paired e conferme 16 → 39. Retry di probe 46 → 108, zero nuovi drop, rifiuti di autenticazione o errori di consegna. La seriale non distingue se la richiesta parte dalla schermata Radar o da Debug. Riscontro specifico dell'operatore su nuova ricerca, assenza di impulsi ai tocchi ripetuti e Debug ancora in attesa.


### Esito finale M3c

**PASS funzionale riferito dall'operatore.** Il collaudo di ritrovamento è completato. Conservate come baseline le app installate S3 M3c e C6 M3b con relativi readback. Taratura M2b.1 e misure strumentali M3a restano aperte.

## M3 — sette sfere fisiche

- Il radar gestisce sette sessioni DRA1 concorrenti con indirizzi, HMAC, contatori, RSSI, freschezza e ritrovamenti per ID. La schermata reale permette di scegliere una sfera associata e mostra il totale da 0/7 a 7/7. I ritrovamenti restano validi dopo perdita radio e standby. **Nuova partita** azzera i ritrovamenti in RAM e richiede nuove misure senza toccare le associazioni.
- La vibrazione da Radar o Debug usa il peer associato all'ID selezionato. Il driver e i tempi M3b sono gli stessi. Nessun cambio a cablaggio, antenna, scala RSSI o RGB.
- DRA1 conserva frame, tipi, nonce e ACK. Per ID 1 restano compatibili firmware e record M3b. Le sfere aggiuntive usano record DRC2 separati. PMK e canale sono condivisi nel dominio e LMK e HMAC sono specifici per coppia. Il core S3 3.3.12 installato espone sette peer ESP-NOW cifrati nel suo sdkconfig. La capacità simultanea reale resta da provare.
- **PASS host** suite di prossimità e radio con sette sfere simulate. Verificati isolamento dell'RSSI, perdita e recupero indipendenti, comando inviato solo a ID 7, replay e duplicati, conteggio 7/7 e nuovo gioco con associazioni intatte. Conservate le regressioni ID 1, M3b, gesture e standby.
- **PASS build** S3 UI Trial, C6 radio ID 7 e utility USB S3/C6 su Arduino ESP32 3.3.12. Il primo tentativo S3 ha rilevato un conflitto tra il nome di un helper e la macro Arduino `bit`, corretto prima della ricompilazione riuscita. Verificati target, checksum, hash d'immagine, tabelle rispetto ai backup ROM e separazione tra radio e provisioning. I binari e i log sono fuori da Git.
  - S3 UI Trial da 1.779.920 byte, app OTA1 a `0x310000`, SHA-256 `4f78dff262259e7bf4ff1e99fdacf390cd9ba231c702a9cc83f053dede8ddcf1`.
  - C6 radio ID 7 da 980.192 byte, app0 a `0x10000`, SHA-256 `c47d1694f73a4e67ff209a426240b35f3ce7cbe8b9737467c9b91a0c674deb32`. Gli ID 2–6 usano lo stesso sketch compilato con il valore assegnato.
  - Utility USB S3 da 378.800 byte, SHA-256 `75f59bd865f146d59eb4ce7fe37d75e87340f252e30854eb825e16dc42aabc41`. Utility USB C6 da 245.472 byte, SHA-256 `b1bf375a08edc30aa72b622d7e7e7243670f9fbf2832c02202baede82bb7e27d`.
- **PASS provisioning simulato** due test del generatore, con estensione DRC1/DRC2, e sei casi del loader USB, compresa scrittura su slot separati e rifiuto della sovrascrittura.
- **NOT RUN fisico** misura dei sette link, provisioning reale e collaudo dei binari. Nessun flash, reset o scrittura NVS in questa fase. Procedura individuale documentata in [PROTOCOL_AND_POWER.md](PROTOCOL_AND_POWER.md).

### M3 multisfera — primo banco con la sola ID 1

- **PASS preflight S3** COM11 identificata, alimentazione solo USB confermata dal PMIC e assetto banco con attuatori isolati. Layout originale, flash da 16 MiB e sola associazione DRC1 ID 1 confermati. Due letture indipendenti dell'intestazione e dell'intero OTA1 coincidono. OTA0 coincide con il backup originale.
- **PASS scrittura e readback** scritti solo 1.779.920 byte della app OTA1 a `0x310000`, SHA-256 `4f78dff262259e7bf4ff1e99fdacf390cd9ba231c702a9cc83f053dede8ddcf1`. Rilettura integrale identica prima dell'avvio. OTA0, NVS, otadata, bootloader, tabella e area OTA1 oltre i settori scritti verificati invariati. C6 M3b intatto.
- **PASS avvio e compatibilità radio ID 1** inizializzati AMOLED e controller touch. DRC1 caricato, solo ID 1 associato e selezionato, conteggio 0/7 nella finestra iniziale. DRA1 Paired per 60,26 s sul canale 6, senza AP e con RSSI valido. Conferme 1 → 9 e 28 retry di probe. Zero drop, rifiuti di autenticazione, errori TX o stalli nella finestra osservata.
- **NOT RUN collaudo fisico con più sfere** il banco usa una sola sfera. La prova RF con trasmittenti non associati e il provisioning degli ID 2–7 restano aperti. Nessuna modifica diretta alla NVS, all'antenna o al C6. Log privati.
- **PASS osservazione senza comandi** per ulteriori 144,66 s la telemetria ha mostrato ID 1 Paired, associato e selezionato, con conteggio 0/7 e comando motore inattivo. Conferme 9 → 32 e retry 32 → 97. Nessun nuovo drop, rifiuto di autenticazione, stallo o errore di consegna osservato.
- Nella finestra seriale non è stata rilevata alcuna transizione di gioco o del comando motore. Le successive verifiche della UI, del gioco e del motorino sono state confermate dall’operatore nel riepilogo seguente.

### M3 multisfera — esito funzionale ID 1

**PASS funzionale secondo l'operatore.** Confermate tutte e sei le prove con la sola sfera 1, inclusi comportamento delle selezioni, vibrazione al ritrovamento, conteggio, esclusione del ritrovamento e Nuova partita. Prova non ripetuta. Conservati S3 multisfera `4f78dff262259e7bf4ff1e99fdacf390cd9ba231c702a9cc83f053dede8ddcf1` e C6 ID 1 M3b `856babf41f4762ec0e2c4eb2b0b5a93624bf9e383788f7d12eacb1072cdbe425`. Il collaudo con più sfere fisiche resta aperto.

## M2c — modalità Reale Aperto

- **Sorgenti verificate offline.** La versione Waveshare 1.75-G ha un Quectel LC76G con antenna GNSS dedicata. Il firmware legge NMEA via I²C 0x50/0x54. UART su GPIO17/18 è un percorso diagnostico opzionale, subordinato al cablaggio e già presente. M2c riusa il fix del driver esistente e non avvia automaticamente la prova UART. Il fix sul banco M2c resta da acquisire. [Documentazione Waveshare](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75)
- **Impostazioni → Radar** cicla Demo, Reale Chiuso e Reale Aperto. Le impostazioni precedenti migrano in lettura da `demo` e conservano la compatibilità con la build precedente. Nessuna riscrittura automatica delle impostazioni al primo avvio.
- In **Aperto**, scegliere un ID associato e toccare **Salva GPS** nel punto in cui si nasconde la sfera. Il comando è disponibile con fix valido e fresco. Registra la posizione del radar per l'ID scelto nel namespace locale `sphere_geo`. Una posizione esistente si aggiorna con la stessa azione. Le coordinate sono facoltative, restano disponibili dopo riavvio e **Nuova partita** conserva l'ultima disposizione. La sfera non trasmette coordinate proprie.
- La vista Aperto riusa distanze geodetiche, proiezione e zoom della Demo. Mostra i riferimenti per le sfere associate, non trovate e con posizione registrata quando il radar dispone di un fix utilizzabile. Usa la bussola calibrata con orientamento Bussola, altrimenti Nord fisso. RSSI, selezione, vibrazione e conferma touch restano disponibili, anche se il GPS non produce un fix. L'RSSI non è convertito in metri.
- **Debug → GPS** espone trasporto disponibile, stato, usabilità del fix, età delle coordinate correnti, qualità, HDOP e satelliti disponibili. La validità per la mappa richiede coordinate finite nel dominio geografico, età massima di 10 s e codice GNSS da 1 a 5.
- **PASS host** 13 esecuzioni, incluse prove di matematica, modalità e migrazione, salvataggio per ID con NVS simulata, mancata scrittura, proiezione, ripiego RSSI e regressioni di prossimità, DRA1, touch e standby. ASan/UBSan su Aperto, prossimità e DRA1.
- **PASS build S3 UI Trial**, Arduino-ESP32 3.3.12. App ESP32-S3 da **1.783.456 byte**, SHA-256 `2eccebca2ac3a2ba9bab342aa15f69e4e4131f9a026e813522ab7c4f15c13651`. Checksum e hash interni validi. La tabella generata coincide con l’ultima tabella del radar archiviata e l’app rientra in OTA1 a `0x310000` (3 MiB). Binario e log conservati localmente.
- **NOT RUN fisico** nessun flash. Il retry elevato dei probe resta in osservazione. Il banco ID 1 precedente ha registrato 28 retry nella prima finestra da 60 s a fronte di zero drop o rifiuti. Nessuna modifica al percorso radio, ai dispositivi C6 o agli attuatori.


### M2c — preflight prima dell’installazione

- **PASS candidato autorizzato `b71a440`** confermati target ESP32-S3, 1.783.456 byte, SHA-256 indicato sopra, checksum e hash interni. Riconfermati i due backup originali da 16 MiB, identici, e gli hash del checkpoint S3 multisfera.
- **PASS controlli USB in lettura** identità S3 riconosciuta su COM11 e app precedente rispondente. Il PMIC segnala batteria scollegata e la diagnostica non riporta un avviso sullo stato inattivo del motorino. AMOLED e controller touch inizializzati. DRA1 con ID 1 Paired sul canale 6, senza AP. Queste osservazioni riguardano l’app precedente.
- **IN ATTESA del banco** occorre confermare che gli attuatori S3 siano fisicamente isolati. Lo stato inattivo del pin non attesta che il carico sia isolato. Il firmware precedente resta in esecuzione. Nessun reset o scrittura sul radar. C6 M3b invariato.
- **NOT RUN** letture correnti in ROM, installazione OTA1, readback e collaudo di M2c. Dopo la conferma del banco, acquisire due copie correnti di intestazione e OTA1, verificare OTA0 e selezione OTA, quindi programmare la sola app già autorizzata e confrontare prima dell’avvio. Il collaudo geografico reale resta rinviato.

## M3d — due LED RGB sulla sfera

- **PASS fisico precedente, firmware temporaneo:** entrambi i WS2812B su GPIO23 accesi in rosso a 3/255 secondo l'operatore. Durata complessiva 120 s in 60 cicli e spegnimento finale confermato via USB. Il C6 usava l’app temporanea al termine di quella prova. Il backup della app C6 M3b è conservato. M2c sul radar resta disponibile come baseline compilata.
- Nel candidato M3d il C6 invia lo stesso pixel due volte su D5. Attesa rossa fino a 8 s, verde per 500 ms al primo Paired, oro per 3 s al ritrovamento, poi spento. Il firmware DEV non inizializza questi attuatori. Il Grove mantiene gli impulsi e il cooldown M3b. Il bianco iniziale a 255 per 500 ms è attivo nel candidato, su richiesta dell’operatore per la prova elettrica. È indipendente dalla luminosità ordinaria. Per una build comparativa si può usare `SPHERE_RGB_STARTUP_WHITE=0`. I LED partono almeno 70 ms dopo l'avvio del feedback motore.
- **Debug → LED sfera** sceglie ID 1–7, luminosità tramite tocco sulla barra, colore e Accendi/Spegni. Il manuale torna spento dopo 10 s. S3 e C6 memorizzano la luminosità ordinaria, inizialmente 50%, sul comando esplicito e soltanto se il valore è cambiato. Il colore diagnostico rimane separato dagli effetti automatici.
- DRA1 aggiunge RGB e RGBResult allo stesso frame autenticato e indirizzato. Il comando ritrovamento usa una richiesta RGB distinta dalla vibrazione esistente, con propria sequenza e deduplicazione. Una sfera M3b ignora i nuovi tipi, senza alterare il pairing, i probe o il motore. La conferma RGB indica accettazione applicativa.
- **RMT verificato su specifica e codice:** il C6 offre due blocchi TX da 48 simboli. Motore e LED prenotano un blocco ciascuno e lo stesso clock sorgente. Il frame LED è asincrono e usa un buffer stabile fino al completamento. Da validare la coesistenza sul dispositivo. [Espressif RMT](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/rmt.html)
- **NOT RUN verifica strumentale M3d.** Nella prova strumentale del bianco al 100%, identificare la revisione, verificare margine VIH a batteria carica e misurare corrente dei due LED, tensioni di batteria e 3V3 e stabilità sotto carico. Per alcune revisioni Worldsemi 12 mA per canale corrispondono a circa 72 mA per due pixel bianchi, a cui si aggiungono elettronica, radio e Grove. È un riferimento di dimensionamento, non una misura del prototipo. Il rosso a bassa luminosità prova la catena dati nel test precedente. [Worldsemi WS2812B V5/W](https://www.world-semi.com/web/userfiles/productfile/WS2812B_V5WDatasheet_V6.1_EN.pdf)
- **PASS host** animazioni RGB, timer e rollover, DRA1 con ASan/UBSan, fixture DRA1, test a sette peer, launcher, Grove, LED USER, RX e M2c Aperto. Verificati tag alterato, MAC errato, risposta persa, retry idempotente e rifiuto di un comando più vecchio.
- **PASS build Arduino-ESP32 3.3.12.** Target, checksum e hash interni verificati. Tabelle generate identiche alle tabelle archiviate. Artefatti e log fuori da Git.
  - S3 UI Trial, OTA1 `0x310000`, 1.785.776 byte, SHA-256 `430c67bacac0ef851de2617665def154c4e063697bed898ead1d3386ca501853`.
  - C6 radio ID 1, app0 `0x10000` (`no_ota`), 988.368 byte, SHA-256 `c8b03185b3e2f080a3a3430a8c05641a04faa3351299a010c7040a38d4747854`.
- **PASS installazione C6 M3d** COM12, due backup correnti completi identici. Scritti 988.368 byte della sola app a `0x10000` e riletti byte per byte. Confermate invariate NVS, tabella partizioni e regioni fuori dai settori della app scritti. Grove scollegato. S3 assente dalle porte USB durante la prova e lasciato al firmware precedente.
- **PASS visivo C6 M3d, riferito dall’operatore:** bianco di avvio seguito da lampeggio rosso. Misure di corrente, durata e tensione sotto carico, prova con Grove, verde Paired, oro ritrovamento e comandi dal radar ancora da eseguire. La riconnessione seriale dopo l’avvio non ha prodotto campioni DRA1 verificabili; la prova visiva resta indipendente da quel controllo.

## M3e — OTA locale C6 e osservazioni RGB

- **Ottica confermata dall’operatore** dentro il guscio arancione, il bianco illumina diffusamente la sfera e assume una tinta arancione. Il blu è quasi invisibile anche al 100%. Il verde è visibile e l’arancione risulta più scuro del bianco. Palette e animazioni M3d invariate. Assorbimento, margine logico e funzionamento con Grove restano da misurare.
- Artefatti e backup M3d conservati. Il C6 fisico resta M3d nel layout `no_ota`. L’app S3 M3d resta compilata e M2c continua nel sorgente. Nessuna modifica hardware o NVS in M3e.
- **Implementato offline** AP locale WPA su richiesta con `ota_start` o via DRA1 indirizzato in due fasi. Versione interrogabile e conferma applicativa distinte dall'osservazione dell'AP e dell'upload. Manutenzione con RGB e Grove spenti. Il PC carica direttamente via browser o CLI. Firma HMAC e credenziale AP derivate localmente per sfera. Layout, ID, versione crescente, target, dimensione e digest verificati prima della selezione. [Procedura OTA](SPHERE_OTA.md)
- **Rollback verificato offline nel loader dedicato** ESP-IDF 5.5.4, opzione rollback attiva e disassemblato con transizioni di otadata. L'app Arduino usa le API di conferma e un termine indipendente di 10 s durante un primo boot pendente. Nessun pairing richiesto. Il comportamento live, compresi interruzione, primo boot fallito e ritorno allo slot precedente, resta NOT RUN.
- I retry dei probe restano in osservazione. Prove al banco M3e, misure elettriche RGB/Grove, GPS sul campo e multisfera fisica ancora aperti.
- **PASS host** CLI/manifest, validazione del layout, DRA1 con ASan/UBSan e fixture, animazioni RGB, Grove, LED USER, launcher, prossimità e M2c. DRA1 copre query versione, indirizzamento, ACK in due fasi e duplicati. Il test host non esercita lo stack Wi-Fi del C6.
- **PASS build S3 UI Trial** 1.786.944 byte, OTA1 `0x310000`, SHA-256 `35ea0d65cab5c3eb4dad8bba8246574b929ee084a6478ccbe446a5bfb3d9e00e`. Tabella uguale al backup S3 e sorgenti di staging verificati.
- **PASS build C6 Arduino 3.3.12** target, checksum e hash interno validi. Entrambe le app rientrano nello slot da 1,75 MiB e le tabelle generate coincidono con quella ESP-IDF. La preparazione della migrazione e degli estratti di ripristino ha passato la verifica sui due dump storici distinti e identici del C6. Le due letture da usare per la migrazione effettiva restano da acquisire.
  - App di migrazione ID 1, versione 301, 1.055.056 byte, SHA-256 `a47f631af9694777a79b6bef66810890ccc5cd1a7b630958e6fc51d6fa2106ce`.
  - App di prova wireless ID 1, versione 302, 1.055.056 byte, SHA-256 `15faafc62fc2c08c367bd9466fdd2062e994e50185b635ab5260337e051e9de3`.
  - Bootloader rollback, 22.592 byte a `0x0000`, SHA-256 `a792902bac3789d4f4ee62c2f0b94d5346f348fb188fe45b0530a95fac722e5e`.
  - Tabella, 3.072 byte a `0x8000`, SHA-256 `b8a579361a5c2bf99e12fb82332892b2cc6c1c2aba69aa043ed733af2b3b02b3`.

### M3e — primo banco C6 ID 1

- **PASS preflight e migrazione** conferma operatore di sola USB con batteria e Grove scollegati. C6 ID 1 su COM12, protezioni compatibili, due nuovi dump integrali da 4 MiB identici, layout `no_ota`, DRC1 presente, otadata originale e SPIFFS vuota. Hash dei quattro candidati e artefatti privati di recupero verificati.
- **PASS readback integrale prima dell’avvio** scritti solo bootloader da 22.592 byte a `0x0`, tabella da 3.072 byte a `0x8000` e app v301 da 1.055.056 byte a `0x10000`. Il nuovo dump intero coincide con la ricostruzione attesa, inclusi tutti i settori fuori dagli intervalli scritti. NVS, otadata e coredump identici al preflight.
- **PASS avvio v301, FAIL inizializzazione radio applicativa** telemetria USB con `configured=0 aligned=0 record_version=0 pmk_result=259 peer_result=261`. Nessuna risposta `OTA_AP_READY` a `ota_start`. Il controllo SSID non ha rilevato l’AP, con capacità della scansione non verificata. L’RGB sul banco sola USB non è stato verificato a vista.
- **FAIL controllo logico NVS dopo il primo avvio** nuova cattura ROM. DRC1 e relativo namespace sono ancora nello stesso settore, byte per byte, e otadata coincide. Il solo settore NVS a `0x9000` presenta 2.239 byte differenti, con intestazione Full → Active e perdita di due definizioni di namespace. Il parser ora segnala un riferimento a namespace mancante. Causa precisa non isolata. Nessun ripristino NVS né ulteriore avvio dopo il riscontro. C6 fermo in ROM con stub RAM.
- **NOT RUN OTA e rollback** nessuna trasmissione v302 o commutazione del Wi-Fi del PC. Preparato offline il candidato privato ID 1 v303 che fallisce deliberatamente l’autoverifica, 1.054.896 byte, SHA-256 `2ea4017cc88a5e48f44953db9eccf8a50d175978b0e5b275f19ba55c42084283`. Target, checksum, hash interno e tabella verificati. Da usare solo dopo ripristino del funzionamento v301, aggiornamento riuscito v302 e autorizzazione specifica.
- Piano per la futura prova di rollback: acquisire backup e verificare v302 VALID, caricare v303 firmata e verificare il ritorno alla v302 con v303 INVALID. In sessioni distinte interrompere un upload prima della selezione e resettare una nuova app in PENDING_VERIFY prima della conferma. Confrontare app attiva, otadata e NVS dopo ciascuna prova. S3 invariato e assente dal banco.

## M3e.1 — recupero NVS C6 ID 1, preparazione offline

- **PASS confronto integrale** i due dump originali sono identici. NVS pre-flash e readback coincidono fino al primo avvio. Dopo v301 cambia solo `0x9000` (2.239 byte). Gli altri settori NVS, la tabella e otadata coincidono. DRC1 e relativo indice sono nel settore `0xc000` invariato. Il parser incrociato con le bitmap trova namespace 4 e 5 mancanti, con riferimenti rimasti nelle pagine precedenti.
- **Diagnosi** la prima pagina passa da Full/0 ad Active/0. La bitmap dimostra cancellazione e riscrittura, con nuovi namespace Wi-Fi/PHY. Le pagine ancora intatte hanno sequenze 6, 19 e 20 e gli stati attesi. Non c'è un ciclo normale di garbage collection compatibile con la sequenza zero e la perdita dei riferimenti. Il codice di errore di apertura/lettura e il chiamante iniziale della cancellazione non erano esposti da v301.
- **PASS confronto eseguibili** sia M3d sia v301 chiamano `__wrap_initArduino` e `nvs_flash_init` senza erase-and-retry. Entrambe le app usano Arduino 3.3.12/IDF 5.5.5 e NVS non cifrata. Il loader dedicato 5.5.4 ha rollback e codice otadata, senza gestione NVS. La configurazione non dimostra incompatibilità di formato NVS. `Preferences` RGB e OTA erano a valle della configurazione DRA1.
- **Correzione di contenimento e diagnosi** blocco delle mutazioni NVS tramite i tre ingressi esp_partition durante mount e avvio. Preflight in sola lettura di namespace, tipo, blob e decoder DRA1 prima del Wi-Fi. Wi-Fi storage RAM. Il log contiene i codici delle API e, per la prima mutazione intercettata, operazione, fase, indirizzo e PC risolvibile nell'ELF. Operazioni 1/2/3 indicano write/write_raw/erase. Fasi 1/2/3/4 indicano mount/apertura/lettura/inizializzazione Wi-Fi e peer. La scrittura viene abilitata dopo successo DRA1 locale, RGB e manutenzione. Il pairing non è necessario.
- Estratti privati, verificati e conservati: NVS originale da 20.480 byte, settore originale `0x9000` da 4.096 byte e NVS dopo v301. Proposto il recupero minimo di un settore più app OTA0 v311, con confronto dell’intera NVS prima del boot. [Procedura di recupero](SPHERE_OTA.md#recupero-m3e1-del-c6-id-1-già-migrato)
- **NOT RUN hardware** il C6 resta in ROM. Nessun reset o scrittura M3e.1. Nessun cambiamento al radar. Occorre misurare i codici effettivi e riverificare l’integrità dopo il prossimo avvio prima di provare OTA v312. Rollback v313 rinviato.
- **PASS software M3e.1** test della barriera NVS, DRA1 con ASan/UBSan, test manifest/layout e tre build Arduino ESP32 3.3.12. I tre ingressi `nvs::NVSPartition` risultano collegati ai wrapper nel disassemblato v311. Target, checksum, hash interni, versione/ID e tabella sono verificati. Il bootloader e i candidati storici sono conservati.
  - C6 v311 recupero USB OTA0, 1.056.432 byte, SHA-256 `84006a3db35182a33d531012c5d8aa738af9bc918932f2931d98d47d1beec6d9`.
  - C6 v312 OTA, 1.056.432 byte, SHA-256 `5b330732213862814f4294b054b2592925fa8cd64f8d2a7319ba0ea04468947d`.
  - C6 v313 fixture privata, 1.056.208 byte, SHA-256 `4c3abf1d0efd3c95a5ebe145044d782937768300db68a1bff4cbc229fedc00e7`.

### M3e.1 — recupero v311 sul C6 ID 1

- **PASS preflight, scrittura e readback.** Su COM12 con sola USB, motore, LED e batteria scollegati per conferma dell’operatore. Ripristinati solo `0x9000–0x9fff` e app v311 da 1.056.432 byte a `0x10000` con fine dei settori scritti a `0x111fff`. Prima dell’avvio il dump intero coincide con la ricostruzione attesa e la NVS con i due originali. Bootloader, tabella, otadata, OTA1 e coredump preservati.
- **FAIL montaggio NVS a v311.** Quattro campioni identici: `init=259 open=259 key=259 read=259 valid=0 version=0 blocked=1 unlocked=0 first_op=3 first_phase=1 first_address=009000 first_pc=42020160`. `nvs_flash_init()` ha propagato `ESP_ERR_INVALID_STATE` restituito dalla barriera. Apertura, ricerca e lettura non sono state chiamate, i relativi 259 sono valori di stato iniziale. Il PC nell’ELF risolve a `nvs::Page::erase()`, in `nvs_page.cpp:1063`. Il tentativo precede Wi-Fi, Preferences e `secure.begin()`. È dimostrato il punto della cancellazione tentata, resta da isolare perché il mount selezioni proprio quella pagina.
- **PASS protezione e integrità dopo boot.** Rilettura ROM: i 20.480 byte di NVS sono identici all’originale. Parser logico, namespace e DRC1 integri. Luminosità invariata (nessun record salvato nei dump). Zero variazioni PHY. Tabella e otadata confermate immutate. C6 lasciato in ROM con stub RAM.
- **NOT RUN** avvio ESP-NOW, RGB, `ota_start`, AP e v312. La barriera ha impedito di arrivare alla radio. Gli attuatori e i LED erano scollegati, quindi manca anche l’osservazione RGB. Nessun OTA o rollback intenzionale. S3 invariato.

## M3e.2 — mount riprodotto offline

- **PASS driver esatto su host.** Arduino core 3.3.12 usa ESP-IDF v5.5.5 `b774170ff46`. Con backend flash simulato, il driver originale classifica correttamente le cinque pagine, monta e legge il DRC1 originale di 76 byte identico, senza tentativi di scrittura o cancellazione. Il parser e il driver concordano sull’originale. Il dump alterato v301 conserva DRC1 leggibile su host, anche se il parser forense segnala riferimenti orfani.
- **PASS riproduzione controllata.** CRC pagina iniziale alterato da solo non basta. Se anche le altre pagine utilizzate non risultano visibili, `PageManager::load → activatePage → Page::erase` tenta la stessa cancellazione di `0x9000` e restituisce 259 dietro la barriera. È un’ipotesi verificabile sui dati letti a runtime, non una causa hardware dimostrata.
- **Confronto offline.** App M3d e M3e.1 collegano la stessa libreria NVS di Arduino. Le routine centrali confrontate coincidono tra il 5.5.4 e il 5.5.5. I bootloader storico e dedicato dichiarano DIO/80 MHz/4 MiB. La diversa immagine loader resta una variabile, senza evidenza conclusiva di incompatibilità.
- Non rigenerata la NVS: l’originale è sana quando il driver vede gli stessi byte del dump. Preparata traccia C6 v321 con confronti di lettura, CRC, classificazione SDK e ramo di erase. Barriera permanente e OTA disattivata in questa variante. [Procedura M3e.2](SPHERE_OTA.md#m3e2--riproduzione-del-mount-e-diagnosi-sul-c6)
- **NOT RUN dispositivo.** C6 in ROM, nessun reset o scrittura M3e.2. NVS originale preservata. Nessun intervento sul radar. v312 e v313 sospese.
- **PASS build e controllo strumenti** C6 ID 1 v321, 1.046.400 byte, SHA-256 `1eec2b0f0fec1d4ccd0967c2bc38d8ccd15fc728524416fe2f916884cf306448`. Offset candidato OTA0 `0x10000`, ultimo settore fino a `0x10ffff`. Tabella identica al layout M3e, checksum/hash immagine validi. Disassemblato conferma i tre wrapper di mutazione, la lettura raw e i punti SDK di load, sequenza ed erase. Test protezione NVS e DRA1 con ASan/UBSan passati. Nessun accesso al dispositivo durante M3e.2.
