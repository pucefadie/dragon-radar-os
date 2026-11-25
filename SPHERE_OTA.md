# M3e — OTA locale delle sfere

## Uso ordinario

L'app C6 avvia STA ed ESP-NOW sul canale configurato. L'AP si apre solo su comando USB `ota_start` seguito da invio, oppure da **Debug → Aggiornamenti OTA** sul radar. Scegliere ID, toccare **Attiva OTA** e confermare. Il radar attende l'ACK autenticato di preparazione prima di inviare il comando di ingresso. Il C6 mantiene attivo DRA1 per la finestra dei retry della risposta, spegne Grove e RGB e sospende il servizio di gioco per quella sfera. Il radar indica l'ACK che ha ricevuto. Il PC verifica separatamente la presenza dell'AP, l'esito dell'upload e il riavvio.

L'SSID è `DragonBall-01` per ID 1, fino a `DragonBall-07`. La chiave WPA e la chiave HMAC di release sono derivate con domini distinti dal segreto della singola sfera. Il PC usa la sua copia privata del **record sfera**, DRC1 o DRC2, in un file 0600. Nessuna nuova credenziale persistente viene scritta. Un PC che possiede il record può firmare release per quella sfera e va trattato come autorità di manutenzione. Conoscere soltanto la password dell'AP non consente di firmare una release. Questo profilo non abilita Secure Boot o flash encryption.

```sh
scripts/sphere_ota.py /percorso/privato/sphere.bin credentials
scripts/sphere_ota.py /percorso/privato/sphere.bin upload /percorso/app-c6.bin 332
```

Connettere il PC all'AP locale prima di eseguire `upload`. Rimanere sulla rete anche se il sistema operativo segnala assenza di Internet. Per il browser, generare un manifest privato e aprire `http://192.168.4.1/`. Selezionare manifest e binario. La barra riflette i byte inviati dal browser. La risposta positiva indica immagine verificata e slot selezionato, da distinguere dall'esito del primo boot.

```sh
scripts/sphere_ota.py /percorso/privato/sphere.bin manifest /percorso/app-c6.bin 332 /percorso/privato/release.json
```

Il manifest autentica il layout M3e, ID, versione crescente, lunghezza e SHA-256. Il ricevitore controlla il chip C6 e cerca il marcatore ID/versione dentro l'immagine autenticata. Le API ESP-IDF convalidano l'immagine prima di selezionarla. Il trasferimento scrive solo nello slot inattivo. L'AP scade dopo 120 s di attesa. Un trasferimento ha un limite separato di 90 s e uno stallo di lettura HTTP causa interruzione. Upload falliti o connessioni interrotte fanno chiudere l'AP e tornare a ESP-NOW. Dopo successo, l'app riavvia il dispositivo.

## Layout e bootloader

| Regione | Offset | Dimensione |
|---|---:|---:|
| Bootloader ESP32-C6 | `0x0000` | fino a `0x8000` |
| Tabella partizioni | `0x8000` | `0x1000` |
| NVS | `0x9000` | `0x5000` |
| otadata | `0xe000` | `0x2000` |
| OTA0 | `0x10000` | `0x1c0000` |
| OTA1 | `0x1d0000` | `0x1c0000` |
| SPIFFS | `0x390000` | `0x60000` |
| Coredump | `0x3f0000` | `0x10000` |

Compilare l'app con Arduino ESP32 3.3.12 tramite `SPHERE_OTA_VERSION=331 scripts/build_m1c_c6.sh ota build/c6-331 1`. L'ID è l'ultimo argomento. Il CSV locale dello sketch è la tabella effettiva. Compilare separatamente il bootloader con ESP-IDF 5.5.4 già attivato tramite `scripts/build_sphere_ota_bootloader.sh build/c6-rollback-loader`. Verificare che le due tabelle generate coincidano. Il loader dedicato è compilato con rollback abilitato. L'app configura un termine di 10 s durante lo startup in stato PENDING_VERIFY. Conferma quando la configurazione locale, ESP-NOW, il peer e l'infrastruttura RGB/OTA sono inizializzati. Il pairing con il radar è indipendente dalla verifica. Un errore rilevato richiede il rollback; un reset prima della conferma permette al loader di marcare ABORTED e ricaricare l'app precedente. La prova di post-reset sul C6 rimane da eseguire. [Espressif OTA e rollback](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32c6/api-reference/system/ota.html)

## Migrazione USB una tantum

1. Dopo autorizzazione, mettere il solo XIAO C6 in ROM via USB, Grove e batteria scollegati. Identificare dispositivo e capacità di 4 MiB, verificare protezioni e porta. Acquisire due dump completi indipendenti e verificare i rispettivi SHA-256 e l'identità byte per byte. Conservare gli originali in locale.
2. Eseguire `scripts/prepare_sphere_ota_migration.py dump-a.bin dump-b.bin partition-table.bin bootloader.bin app-331.bin directory-privata-nuova`. Il controllo richiede il layout no_ota storico, selezione OTA0 valida e SPIFFS vuota. Per SPIFFS con dati, predisporre una migrazione dati prima di scrivere. Il controllo sul vecchio dump è di fattibilità. Al banco servono due nuovi dump.
3. Con autorizzazione esplicita alle tre scritture, usare esptool in ROM con reset finale disabilitato. Scrivere solo bootloader, tabella e OTA0. Usare gli artefatti privati verificati e la porta riconfermata. NVS, otadata e coredump restano fuori dall'operazione. L'area OTA1 va popolata dal successivo upload firmato.

   ```sh
   esptool --chip esp32c6 --port PORTA_C6 --before no-reset --after no-reset write-flash \
     --flash-mode keep --flash-size keep --flash-freq keep \
     0x0 migration-0x0000.bin 0x8000 migration-0x8000.bin 0x10000 migration-0x10000.bin
   ```

4. Rileggere e confrontare tutti i byte scritti e verificare NVS e otadata identiche prima del primo avvio. Dopo il boot, leggere versione e stato e accertare che l'AP resti spento finché non richiesto.
5. Per tornare al layout precedente, entrare in ROM. Con autorizzazione di ripristino, verificare gli hash e riscrivere i quattro file `restore-0x0000.bin`, `restore-0x8000.bin`, `restore-0xe000.bin` e `restore-0x10000.bin` ai rispettivi indirizzi, con gli stessi parametri e senza reset finale. La regione da `0x10000` ricopre vecchia app, vecchia SPIFFS e possibili scritture del nuovo OTA1. Verificare ognuna prima di avviare. NVS e coredump restano fuori dal ripristino. I dump completi sono conservati per recupero ROM separato.

## Collaudo

- **Solo C6** dopo migrazione autorizzata, verificare assenza AP all'avvio, `ota_start`, WPA, timeout senza upload, firma/ID/versione/chip errati, upload interrotto, 301 → 332, primo boot, stato OTA, DRC1 e luminosità persistenti. Verificare il ritorno a OTA0 con una release di banco firmata con autoverifica deliberatamente fallita. Verificare separatamente reset o perdita alimentazione durante ricezione e prima della conferma del boot.
- **C6 + S3** verificare pagina Debug, query versione, conferma e indirizzamento della richiesta, compatibilità con C6 precedenti, ripresa DRA1 e funzioni RGB. La verifica che le altre sfere restino operative richiede più di una sfera fisica.
- Le misure RGB/Grove, il GPS sul campo e il collaudo multisfera fisica restano prove separate.

## Recupero M3e.1 del C6 ID 1 già migrato

Il reset dopo la migrazione v301 ha lasciato il settore NVS `0x9000` con intestazione Active e sequenza 0, al posto di Full con sequenza 0. I settori `0xa000–0xdfff` sono identici, inclusi pagina Active 20, namespace `dragon_radio` e blob/index DRC1. La bitmap della prima pagina è passata da 20 slot scritti e 106 cancellati a 70 scritti e 56 vuoti. Il passaggio di bit da 0 a 1 implica una cancellazione del settore. Il nuovo contenuto include namespace Wi-Fi/PHY riallocati e lascia riferimenti orfani nelle altre pagine. La normale GC su pagine riconosciute dal PageManager assegnerebbe una sequenza successiva a 20. Il dump, quindi, mostra una reinizializzazione anomala della prima pagina. Non identifica il chiamante né il codice NVS restituito durante v301. [Formato NVS Espressif](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/storage/nvs_flash.html)

M3d e M3e usano lo stesso SDK Arduino 3.3.12, con app ESP-IDF 5.5.5, `nvs_flash_init()` senza erase-and-retry e NVS non cifrata. Il bootloader dedicato ESP-IDF 5.5.4 è DIO/80 MHz, usa la tabella a `0x8000` e le routine di mutazione otadata. Il binario non incorpora il gestore NVS. Le configurazioni NVS non dimostrano un’incompatibilità di formato. In v301 la telemetria si ferma prima della riuscita di `secure.begin()`; `Preferences` e la derivazione credenziali OTA vengono invocate dopo quel gate.

La build v311 blocca `esp_partition_write`, `esp_partition_write_raw` e `esp_partition_erase_range` sulla NVS fin dal mount. Prima di accendere il Wi-Fi verifica il layout, apre il namespace in sola lettura e valida il record con lo stesso decoder del collegamento. Registra i soli codici `init/open/key/read` e il numero di mutazioni bloccate. Il Wi-Fi usa storage RAM. Le scritture normali diventano disponibili dopo l’inizializzazione locale di peer DRA1, RGB e manutenzione. Una verifica fallita lascia la protezione attiva. Questo contiene il difetto osservato e rende misurabile l’errore nel prossimo avvio.

Per il **solo C6 già migrato**, attendere autorizzazione alle due scritture seguenti.

1. Con sola USB, Grove e batteria scollegati, verificare in ROM identità, 4 MiB e stato corrente. Confrontare l’intestazione con il dump post-v301 e i settori NVS `0xa000–0xdfff` con i due dump originali. Verificare che la tabella resti M3e, OTA0 selezionata, OTA1 intatta e che la differenza NVS rimanga limitata a `0x9000`. In presenza di altre differenze, ricalcolare il piano prima di scrivere.
2. Ripristinare il **solo settore da 4.096 byte a `0x9000`** usando `restore-sector-0x9000.bin`. La copia integrale privata `restore-nvs-0x9000.bin` da 20.480 byte resta disponibile per recupero separato. Scrivere la sola app v311 a `0x10000`. Usare il comando esptool riportato sopra con gli stessi parametri, sostituendo le coppie indirizzo/file con `0x9000 restore-sector-0x9000.bin 0x10000 v311.bin`. Bootloader, tabella, otadata, OTA1 e coredump restano fuori dalle scritture.
3. Rileggere l’immagine e confrontare l’intera NVS con l’originale prima di avviare. Verificare `M3E_NVS init=0 open=0 key=0 read=0 valid=1 version=1`, `configured=1` e assenza dell’AP fino a `ota_start`. Conservare i contatori e confrontare di nuovo NVS e DRC1 dopo il boot. Se mount o lettura falliscono, usare i codici numerici e la rilettura in ROM per localizzare il percorso. Interrompere la prova prima di qualsiasi altro intervento di recupero.
4. Solo dopo tale verifica, provare `DragonBall-01`, timeout e trasferimento firmato v311 → v312. Verificare readback OTA1, stato VALID, DRC1 e impostazioni. Tenere il candidato v313 con autoverifica deliberatamente fallita per una sessione di rollback separata.

### Risultato del recupero v311

Il recupero del solo settore a `0x9000` e di OTA0 ha superato il readback. Al mount successivo la barriera ha intercettato una cancellazione di `0x9000` da `nvs::Page::erase()`, PC `0x42020160`, prima di Wi-Fi e dell’apertura del record DRA1. L’intera NVS è rimasta identica dopo il primo avvio. La sfera è in ROM e v312 resta sospesa. Una nuova sessione dovrà localizzare il percorso che porta `PageManager` a cancellare il settore, confrontando le letture delle intestazioni dal firmware e dalla ROM e l’esito di classificazione a runtime. Conservare la barriera per quella prova. Il ripristino di `0x9000` è già riuscito e va ripetuto solo se un nuovo readback lo richiede.

## M3e.2 — riproduzione del mount e diagnosi sul C6

Il pacchetto Arduino ESP32 3.3.12 identifica il proprio SDK come ESP-IDF v5.5.5, commit `b774170ff46`. Il banco host compila `Page`, `PageManager`, `Storage` e i tipi NVS da quel commit. Emula soltanto flash, logging e CRC ROM (zlib con lo stesso seed). Il backend rifiuta e conta scritture e cancellazioni. Il test accetta un blob privato di riferimento e confronta tutti i 76 byte DRC1 senza stamparli.

```sh
tests/nvs_mount/run.sh /percorso/esp-idf-v5.5.5 /percorso/privato/nvs-originale.bin /percorso/privato/sphere.bin
```

### Catena e risultati

`nvs_flash_init()` individua il descrittore della partizione e il manager costruisce `Storage`. `Storage::init()` chiama `PageManager::load()` prima di caricare namespace e indici blob. `Page::load()` legge l’header da 32 byte, verifica CRC sui byte 4–27 e versione. Per le pagine in uso legge bitmap e slot; per un header vuoto verifica che la pagina sia tutta cancellata. `PageManager::load()` separa le pagine con sequenza valida dalle pagine libere e ordina per sequenza. La riparazione di duplicati e il completamento di una pagina FREEING avvengono in questo percorso. La raccolta ordinaria su richiesta di spazio avviene in `requestNewPage()`. Se durante il mount nessuna pagina ha sequenza valida, `activatePage()` parte da zero e cancella la prima pagina disponibile se classificata CORRUPT. Dopo il mount, `Storage` confronta indici e chunk e gestisce blob orfani. [Codice ESP-IDF v5.5.5](https://github.com/espressif/esp-idf/blob/b774170ff46c393eeb5e495ea37936038d3f4f4f/components/nvs_flash/src/nvs_pagemanager.cpp) · [Formato NVS](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32c6/api-reference/storage/nvs_flash.html)

- **Originale su driver host:** Full/0, Empty, Active/20, Full/6, Full/19. Corrisponde al parser offline. Mount riuscito, zero mutazioni, apertura namespace riuscita e DRC1 identico. Una NVS rigenerata non è giustificata da questi dati.
- **Dump v301 sul driver host:** mount e lettura di DRC1 riusciti anche con due pagine Active. Il parser forense rifiuta invece l’insieme dei record con namespace orfani. Il successo del driver non certifica che tutti i record di sistema siano recuperati.
- **Controlli di isolamento:** corrompere soltanto il CRC della prima pagina nella copia host non produce l’erase osservato. Rendere anche invisibili le altre tre pagine con sequenza valida riproduce `erase offset=0`, un solo tentativo e mount 259 con barriera attiva. È una riproduzione della firma del guasto, non una misura delle letture sul C6.

Le app M3d e M3e.1 collegano la stessa libreria Arduino `libnvs_flash.a`. Le routine centrali NVS esaminate coincidono tra 5.5.4 e 5.5.5. Entrambi i bootloader letti dagli artefatti dichiarano flash DIO, 80 MHz, 4 MiB. Il loader dedicato differisce dal precedente come binario e resta da verificare il comportamento concreto delle letture a runtime; gli header non dimostrano che controller, cache e percorsi di accesso restituiscano gli stessi dati.

### Una sola sessione strumentata da autorizzare

La build USB di diagnosi si prepara con `SPHERE_OTA_NVS_TRACE=1 SPHERE_OTA_VERSION=321 scripts/build_m1c_c6.sh ota build/c6-trace 1`. La barriera resta chiusa per tutta la sessione e il candidato non apre l’AP. Registra gli header realmente letti durante il mount, i risultati di `Page::load()` e `getSeqNumber()` e il PC del chiamante di `Page::erase()`. Dopo il mount confronta `esp_partition_read_raw`, `esp_flash_read` e mappatura CPU, con CRC ROM e software. Log privati.

Dopo autorizzazione, verificare via ROM che NVS sia ancora identica. Scrivere soltanto la v321 in OTA0 a `0x10000`, fare readback e acquisire la traccia di un solo avvio. Rileggere la NVS e lasciarla intatta. Se le letture divergono, risalire al percorso di lettura o alla configurazione flash; se coincidono, usare classificazioni e PC per verificare il ramo interno specifico. Nessuna scrittura di `0x9000` necessaria con lo stato attuale. OTA v312 e rollback v313 restano sospesi.

### Riscontro v321 e loader con profilo flash coerente

Il readback ROM prima e dopo il singolo avvio conferma che le cinque pagine NVS sono integre. Nel firmware, lettura raw e diretta restituiscono lo stesso contenuto errato in tutte e cinque le pagine, con API di lettura a zero. I cinque CRC delle pagine mappate coincidono con le acquisizioni ROM. Il CRC ROM e quello software concordano sugli header effettivamente letti. La classe NVS scarta tutte le sequenze e `activatePage()` tenta di cancellare la prima pagina CORRUPT, bloccata dalla barriera. Il problema è a monte della NVS e il blob DRC1 non va ricostruito.

L’app Arduino usa l’archivio `qio_qspi/libspi_flash.a`, mentre il primo bootloader dedicato era configurato DIO. La revisione del loader abilita QIO per rendere coerente il percorso comando flash con l’app. L’header dell’immagine del loader può continuare a indicare DIO perché la ROM lo usa per il bootstrap prima che il codice abiliti QIO. Confrontare `sdkconfig` ed ELF, oltre all’header. [Inizializzazione flash ESP-IDF](https://github.com/espressif/esp-idf/blob/b774170ff46c393eeb5e495ea37936038d3f4f4f/components/spi_flash/esp_flash_spi_init.c)

Per una futura prova controllata mantenere v321 e il layout M3e. Dopo nuova autorizzazione, salvare la regione del loader e installare soltanto il loader rollback QIO a `0x0000`. Rileggere integralmente la regione, verificare che il resto della flash coincida e acquisire la stessa traccia a un solo avvio. Successo richiede corrispondenza con la ROM per raw, direct e mapped, mount e lettura DRC1 riusciti, zero mutazioni NVS e barriera ancora chiusa. Il loader storico contiene il percorso QIO ed è un controllo comparativo possibile con la stessa app, ma introdurrebbe più differenze del loader dedicato. Tenerlo come prova di secondo livello, con autorizzazione separata e senza attivare OTA. Nessuna nuova scrittura a `0x9000` prevista.

### Loader QIO verificato sul C6 ID 1

La comparativa ha sostituito solo il loader DIO con il loader rollback QIO da 23.632 byte a `0x0000`. Esptool in modalità keep per tutti i parametri flash. App v321 e tabella identiche. Sul singolo avvio, le tre letture delle cinque pagine coincidono con i dump ROM, le sequenze sono valide per le quattro pagine in uso, `nvs_flash_init()` e DRC1 restituiscono successo. Nessuna cancellazione intercettata. La barriera ha bloccato una normale richiesta di scrittura dopo il mount da `Page::writeEntry()`. L’intera NVS è identica dopo il ritorno in ROM. La comparativa con il loader storico non è necessaria per confermare il mismatch DIO/QIO osservato.

Per ID 1 con loader QIO già verificato, il prossimo candidato operativo è **v331** in OTA0. La build ordinaria richiede uguaglianza byte per byte tra lettura raw e mappata dell’intera NVS prima del mount. Il log `flash_read_check=0` conferma tale precondizione. Il confronto è un controllo di integrità del bus, non sostituisce la validazione DRC1. Non riscrivere loader, tabella, NVS o otadata nella prossima installazione. Dopo autorizzazione e readback, verificare avvio normale, radio, assenza AP all’accensione e `ota_start`. Solo a verifica superata e dietro autorizzazione, caricare v332 nello slot inattivo come primo aggiornamento firmato. Le precedenti v312 e v313 restano sospese. Il test di rollback richiede una fixture successiva e autorizzazione dedicata.

### Primo OTA operativo verificato

Sul C6 ID 1 con loader QIO, v331 è stata installata via USB solo in OTA0. `ota_start` ha aperto DragonBall-01 e il PC ha raggiunto la pagina v331. Dopo 120,0 s senza upload l’AP si è chiuso ed ESP-NOW è ripartito. Il successivo invio firmato v332 è stato completato direttamente via Wi-Fi, con conferma del server e riavvio automatico. Rilettura ROM byte per byte in OTA1 a `0x1d0000`, sequenza 2 VALID, NVS identica, OTA0 v331 preservata. La sfera è stata poi riavviata in v332 con `flash_read_check=0`, ESP-NOW operativo e AP spento.

La prossima prova di rollback usa una fixture privata v333 che forza l’esito negativo dell’autoverifica al primo boot, senza sbloccare le scritture NVS. A partire da v332 VALID in OTA1, previa autorizzazione specifica e nuovo backup, il trasferimento firmato scriverebbe v333 nell’OTA0 inattivo. Attendersi il rifiuto della nuova app e il ritorno automatico alla v332. Verificare entrambe le entry OTA, il readback della v332 e la NVS. Questa prova sostituisce v313, che resta sospesa con v312. V333 non va usata come release operativa.
