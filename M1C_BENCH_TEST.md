# M1c — collaudo su banco in preparazione

Il preflight sulle schede collegate è autorizzato in sola lettura. Programmazione, provisioning e ripristino richiedono approvazione distinta. Baseline M1b `a43bfe7`. Vedere [STATUS.md](STATUS.md) per i risultati correnti e [SECURITY_PLAN.md](SECURITY_PLAN.md) per la decisione sulle chiavi.

## 1. Predisposizione e limiti elettrici

1. Identificare il Waveshare ESP32-S3-Touch-AMOLED-1.75-G e il Seeed XIAO ESP32-C6. La configurazione S3 corrente dichiarata dall'operatore è senza batteria, con BOOT accessibile e motore, bussola e pulsanti esterni isolati. Usare entrambe le schede solo via USB, con gli attuatori isolati. Collegare l'antenna U.FL del C6 prima di alimentarlo. Preparare la distanza 1–2 m per la sessione radio successiva.
2. Sul radar usare l'ingresso USB previsto e lasciare liberi i pin di alimentazione di espansione. PWR comanda l'AXP2101. Se in futuro viene ricollegata una batteria, togliere USB può lasciare acceso il SoC. Se si osservano reset anomali, vibrazione o instabilità, interrompere la sessione. [Riferimento hardware Waveshare](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md)
3. Il repository assegna al motore il segnale attivo HIGH su GPIO16. Waveshare identifica quel pin di espansione, ma la documentazione del SoC non gli garantisce un LOW pilotato al reset. GPIO0 seleziona il download, GPIO46 è uno strap e i pin USB nativi sono GPIO19/20. ROM, bootloader e transizioni del PMIC precedono la protezione applicativa. Non è verificata la polarizzazione del driver motore effettivamente montato né il suo comportamento durante reset, watchdog o inserzione USB con batteria collegata. Il readback digitale della build di banco non esclude impulsi transitori. [Configurazione GPIO Espressif](https://documentation.espressif.com/api/resource/path/docs/projects/esp-hardware-design-guidelines/en/latest/esp32s3/esp-hardware-design-guidelines-en-master-esp32s3.pdf)
4. Il firmware standard resta fuori dal piano M1c. Lo sketch separato `examples/dragon_m1c_bench` compila la sola radio e una console con tre comandi. Anche l'immagine separata di provisioning S3 usa lo stesso avvio protetto. La build mette il latch GPIO16 a 0 prima di abilitare l'uscita con pull-down e input attivo. Se il readback non è LOW, non avvia radio o provisioning. Non ci sono demo, pulsanti, dispatcher standard, timer motore, diagnostica interferenze, audio, display, filesystem o comandi per attuatori.
5. Il Wi-Fi S3 di banco è STA senza associazione, con configurazione Wi-Fi in RAM. Parte fisso sul 6, come la build C6 con antenna esterna. Solo in questa app isolata `channel1` e `channel6` consentono il disallineamento controllato senza cambiare l'AP o le credenziali esistenti.

### Audit software degli azionamenti S3

| Percorso nel firmware standard | Esclusione nel banco |
|---|---|
| Avvio e stato GPIO16 in `peripherals_module.cpp`; driver attivo HIGH e timer di spegnimento | Il driver e i timer non sono collegati. Il solo avvio banco preimposta LOW, abilita output e verifica il livello. |
| Demo e feedback durante la navigazione, hold in Debug, feedback pulsante nel loop periferiche | Nessun app manager, touch, demo, radar haptic o handler pulsante. |
| `vibe*`, `testall`, test 4/test 6, fallback diretto su GPIO16 nel test magnetico | Nessun dispatcher diagnostico o codice sensori. Anche il fallback HIGH senza driver motore è assente. |
| Audio, beep, speaker e GPIO46 via codec/I²S | Moduli e dispatcher audio esclusi. La build non pilota GPIO46. |
| Errori e timer: stop/watchdog del driver standard | Nel banco un errore di avvio impedisce l'uso delle funzioni. Il loop si ferma se GPIO16 non è letto LOW. Non c'è un timer che possa attivarlo; reset e brownout rimangono separati. |

## 2. Preflight in sola lettura, ROM senza reset automatico

**Ingresso S3 verificato nella configurazione USB.** Senza batteria e con attuatori isolati, l'operatore ha tenuto BOOT premuto durante il collegamento USB, poi la sonda senza reset ha confermato la ROM. Lasciare il radar in ROM durante le letture. Se cambia l'alimentazione o viene ricollegato un attuatore, rivalutare la transizione prima di qualsiasi reset o avvio. [Procedura download Waveshare](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75/Firmware-Flashing)

Dopo conferma esplicita, identificare una scheda alla volta per enumerazione USB e risposta ROM. Le porte COM11 (S3) e COM12 (C6) sono soltanto storiche; COM5 è estranea. Non aprire monitor applicativi per sondare l'identità. S3 già stabile in ROM può essere inventariato senza avviare l'app e senza riattivare la radio. Su C6, senza batteria e con attuatori esterni isolati, predisporre la ROM con BOOT secondo la procedura del produttore. Ricontrollare USB padre/porta e chip in ogni fase. Gli indirizzi STA servono solo per il successivo provisioning e restano privati. [Ingresso ROM S3](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html) · [Seeed XIAO C6](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)

Usare esptool 5.3.1 con `--chip <chip-verificato> --port <porta-verificata> --before no-reset --after no-reset --no-stub` per `get-security-info`, `flash-id`, `read-flash`. Per inventario eFuse usare esclusivamente `espefuse --chip <chip-verificato> --port <porta-verificata> --before no-reset --after no-reset summary`. I segnaposto non sono comandi pronti. Il pacchetto solo ROM non deve programmare flash o eFuse né fare reset. Se la ROM non risponde o non supporta una lettura, sospendere il target; nessun reset automatico né caricamento stub come ripiego implicito. Lasciare il S3 in ROM. [Opzioni esptool](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/advanced-options.html)

Controllare dimensione e protezioni flash e leggere almeno `0x0/0x9000` per bootloader e tabella. Acquisire due letture indipendenti di app per entrambi gli slot S3 e unica app C6, di NVS e otadata e confrontarle integralmente con `cmp` e SHA-256. Se una lettura lunga non è affidabile, usare blocchi ROM con indirizzi e lunghezze verificati e ricomporre due copie. Conservare manifest e log fuori da Git. Registrare lo slot selezionato, il CRC/stato delle due entry e l'identità delle immagini lette. Il firmware effettivamente in esecuzione e il canale live restano **NOT RUN** nel preflight in ROM; si verificano al successivo avvio autorizzato.

| Target | Evidenza storica da ricontrollare | Piano da usare solo se la lettura corrente coincide |
|---|---|---|
| S3, 16 MiB | NVS `0x9000/0x5000`, otadata `0xe000/0x2000`, OTA0 `0x10000/0x300000`, OTA1 `0x310000/0x300000`. Dopo il trial RX, OTA0 integro; OTA1 conteneva il ricevitore minimale ed era selezionato con sequenza 4 VALID. | Conservare OTA0. App di provisioning e poi app radio, entrambe versioni protette M1c, nel solo OTA1. Se OTA1 risulta ancora selezionato con record stabile, non riscrivere otadata. Se è selezionato OTA0, predisporre una selezione esplicita, verificata dal preflight corrente e autorizzata separatamente. |
| C6, 4 MiB | App0 `0x10000/0x200000`, NVS `0x9000/0x5000`, otadata `0xe000/0x2000`, SPIFFS `0x210000/0x1e0000`, coredump `0x3f0000/0x10000`. Un solo slot. L'ultimo firmware osservato era DEV con antenna interna. | Usare immagini con profilo `esp32:esp32:XIAO_ESP32C6:PartitionScheme=no_ota` confrontate con la tabella archiviata. La sostituzione di app0 richiede una copia integrale e corrente dello slot e un recupero ROM pronto. Gli artefatti C6 M1b a due slot non sono la scelta di banco. |

Prima di proporre qualunque scrittura, verificare due copie indipendenti e identiche dell'app che verrà sostituita, della NVS completa e di otadata. Verificare anche tabella, bootloader, integrità dell'app preservata e disponibilità del backup completo per ogni scheda. Per il C6 usare le due copie live, che includono app e NVS aggiornate rispetto allo storico. Conservare tutto fuori Git in directory private. Annotare intervallo, dimensione e SHA-256 e confrontare l'intervallo di cancellazione arrotondato a 4 KiB con i confini dello slot. Se la console app manca, la tabella diverge, la ROM non è raggiungibile o l'identità è ambigua, fermarsi.

## 3. Provisioning e installazione — fasi da autorizzare

Prima della futura sessione autorizzata, predisporre una directory privata fuori dal repository. Dopo aver confermato gli indirizzi STA e il canale 6 dei candidati, generare una coppia di record nuova con `umask 077` e:

```bash
python3 scripts/make_radio_records.py "$HOME/.local/share/dragon-radar-os/records-m1c-sessione-nuova" --channel 6
```

Il percorso deve essere nuovo. Il generatore crea `radar.bin` e `sphere.bin` da 76 byte, con LMK e chiave HMAC condivise e PMK locale differente. Non stampare, inviare a terzi o conservare in log i payload. NVS in chiaro è accettata solo per questi segreti temporanei al banco. La generazione dei record reali e l'invio USB non fanno parte della preparazione odierna.

1. Approvare un piano per scheda con evidenza elettrica per ogni avvio o reset S3, porta verificata, hash dell'app, offset, intervallo di erase implicito, app originale e recupero ROM. Usare solo il file app `.ino.bin`. I file merged, bootloader, partitions, immagini NVS e comandi generici di upload non rientrano nel piano.
2. Installare e rileggere l'utility cablata nel solo slot indicato. Sul radar usare esclusivamente il candidato con entrambi i marker `M1C_BENCH_MODE=1` e `M1C_BENCH_PROVISION=1`, dopo il gate elettrico e l'approvazione del binario. Ricontrollare i metadati OTA. Mantenere o selezionare solo un record stabile `VALID` se confermato dal bootloader reale: `NEW` e `PENDING_VERIFY` possono causare scritture autonome; non aggiornare `0xe000`. Prima di inviare dati verificare che l'utility sia quella attesa. La utility C6 usa l'avvio Arduino normale, che può tentare il recupero NVS in condizioni di errore. L'avvio protetto S3 non esegue l'erase-and-retry del core: si ferma se NVS non si inizializza. La normale gestione NVS/PHY può comunque scrivere metadati e calibrazione; si preservano le impostazioni tramite catture e piano di recupero, senza promettere NVS byte-identica. L'autorizzazione futura deve coprire il provisioning e il relativo rischio; se è richiesta NVS immutabile, l'utility attuale non soddisfa il requisito.
3. Con entrambi i monitor chiusi, inviare per ciascuna porta identificata, a 115200, **in una sola scrittura binaria** il byte ASCII `P` seguito dal file per quel ruolo. Non aggiungere newline. Attendere l'esito `RADIO_PROVISION_OK` entro il timeout. A `RADIO_PROVISION_REJECTED`, fermarsi e non cancellare il record esistente. L'utility non sovrascrive un'associazione già presente. Conservare solo l'esito nei log.
4. Rientrare in ROM, sostituire l'utility con l'app radio protetta S3 o l'app standard C6 verificata nello stesso slot, rileggere e confrontare esattamente l'immagine. Solo dopo verificare la selezione e avviare normalmente. Registrare i canali e controllare che le due app partano con `configured=1`. Usare `beacons` sul radar; la sfera stampa lo stato ogni 5 s.

## 4. Matrice di prova futura

Osservare contemporaneamente entrambi i terminali con timestamp. `state=2` attende una sessione autenticata, `3` indica Paired, `4` Lost. `confirmed` è un contatore locale: sul radar cresce all'ACK applicativo accettato, sul C6 alla prima accettazione di un probe. L'esito TX del driver e i beacon DRB2 non provano l'autenticazione DRA1.

Le immagini con i contatori DRA1 mostrano anche `probe_retry_queued` sul radar e `duplicate_probe` sulla sfera. Il primo conta i retry accettati nella coda locale, il secondo le ripetizioni dell'ultimo probe ricevute con HMAC valido nella sessione corrente. `auth_rejected` conta i tag errati dopo i controlli di formato e indirizzo. `stale_session` conta ACK o probe con HMAC valido e nonce diversi da quelli della sessione aperta. Per ogni prova annotare le differenze fra due letture, insieme a `confirmed`.

La callback ESP-NOW accoda i frame. Il loop applicativo esegue la ricezione DRA1, l'avanzamento della sessione e la lettura dei suoi contatori. Un tentativo di retry rifiutato dalla coda TX consuma il tentativo del tracker e lascia invariato `probe_retry_queued`.

| Caso | Azione futura | Criterio |
|---|---|---|
| Handshake, canale e ACK | Tenere le schede a 1–2 m, canale effettivo 6, acquisire almeno 60 s | `configured=1 aligned=1 state=3` su entrambi; cresce `confirmed` su entrambi; registrare drop e `tx_stalled`. |
| Riavvio radar | Riavviare solo S3, C6 alimentato | Il radar rilegge l'associazione, apre una nuova sessione e riprende gli ACK. Registrare tempi e contatori prima/dopo. |
| Riavvio sfera | Riavviare solo C6, S3 alimentato | Il radar può mostrare Lost dopo la scadenza di circa 15 s; ritorno a Paired e probe confermati senza riprovisionare. |
| Perdita e ritorno | Scollegare USB C6 per almeno 20 s, riconnetterlo | Lost sul radar durante la perdita; nuovo handshake e ACK al ritorno. |
| Cambio e ripristino canale | Inviare `channel1` al solo S3 di banco, attendere il timeout, poi `channel6` | Sul S3 `aligned=0`, conferme ferme e scadenza; al ritorno `aligned=1` e recupero. Il C6 non cerca altri canali. |
| ACK perso e retry | Fixture dedicata che sopprime il primo ACK applicativo mantenendo attiva la sessione | Rinvio dello stesso probe dopo circa 450 ms, ACK finale, una sola accettazione sul C6. |
| Tag errato, duplicato e replay | Fixture autorizzata che invia tramite il proprio peer cifrato un tag DRA1 alterato, poi un duplicato della sessione e una richiesta vecchia dopo nuovo handshake | Nessuna conferma o refresh per tag errato o richiesta vecchia. Il duplicato corrente può ricevere l'ACK già prodotto, senza incrementare le accettazioni né rinnovare la sessione. |

I test host verificano ACK perso, tag errato e un probe autenticato appartenente a una sessione precedente. Sul banco la soppressione di un ACK e l'invio controllato dei frame negativi richiedono ancora una fixture dedicata e approvata. La verifica indipendente di CCMP richiede una cattura radio. Tenere chiavi, nonce e frame completi fuori dai log. Acquisire un campione valido prima di ogni prova negativa. Se non compare Paired dopo 30 s sullo stesso canale, conservare il log e stop. Ripetere la prova dopo aver identificato la causa.

## 5. Chiusura e rollback

- Rientrare in ROM e confrontare bootloader, tabella, otadata, slot preservato e NVS prima/dopo. La NVS durante M1c è attesa **diversa** a causa del provisioning. L'audit NVS immutata del ricevitore minimale precedente non copre questa prova.
- S3: per ripristinare lo stato del preflight, rimettere l’intero contenuto OTA1 catturato se è stato sostituito e verificare in lettura. Se la selezione OTA è cambiata, ripristinare il solo settore modificato e verificare entrambi i record. Un ritorno al firmware OTA0 preservato è un’alternativa distinta, non equivale al ripristino dell’eventuale app OTA1 precedente. Tutte le scritture e il riavvio di recupero richiedono approvazione e gate elettrico. C6: ripristinare l'intera app0 catturata prima del banco; senza OTA1 il recupero è in ROM. Verificare in lettura e poi riconoscere la vecchia app al boot. Nessun ripristino integrale del chip.
- Il ripristino dell'app o della selezione non revoca i record NVS. Pianificare la rimozione mirata dei record di banco o, se espressamente autorizzato e verificato, il ripristino della NVS precedente. Non riutilizzare chiavi temporanee; eventuali vecchi dump restano dati sensibili. Non cancellare NVS per risolvere errori o rotazione.
- Esito da registrare per ogni riga: PASS/FAIL/NOT RUN, commit e hash, stato iniziale/finale, canali, timestamp, limiti e ritorno al firmware precedente. M1 resta aperta finché le prove richieste non sono documentate.
