# M3f — aggiornamenti OTA del radar

## Uso

Aprire **Debug → Aggiornamenti OTA**. **Sfera** mantiene il percorso DRA1 M3e. Per il radar scegliere **Radar → Attiva OTA → Conferma**. L’AP **DragonRadar** si apre solo a seguito di questa conferma, con il radar in STA scollegato dall’AP domestico e senza portale di configurazione attivo. Per il collaudo usare la build UI Trial, che parte offline. L’AP usa il canale effettivo del radar. La manutenzione non richiede Internet o un router.

Collegare il PC a DragonRadar con la password WPA2 indicata sul display e mantenere la connessione anche quando il PC segnala assenza di Internet. Il display mostra anche una chiave manifest di 32 cifre esadecimali su due righe. Le due chiavi sono indipendenti, casuali per ciascuna sessione e mantenute in RAM. La chiave manifest autorizza la release corrente e va digitata soltanto nel prompt del PC, senza inserirla in shell, log o repository.

Per l’invio diretto, con il binario applicativo M3f verificato e di versione superiore a quella installata:

```sh
python3 scripts/radar_ota.py upload CANDIDATO_S3.bin
```

Per l’invio da browser, generare il manifest nella sessione corrente. Aprire **http://192.168.4.1/**, selezionare il manifest e l’app e toccare **Carica**.

```sh
python3 scripts/radar_ota.py manifest CANDIDATO_S3.bin --output DIRECTORY_PRIVATA/release.json
```

Il manifest HMAC-SHA-256 lega formato M3f, chip, versione, dimensione e SHA-256 dell’intero file. Il radar verifica autorizzazione e lunghezza prima di iniziare la ricezione; verifica target, profilo flash e descrittore dell’app prima della prima scrittura. L’immagine completa deve passare la verifica SDK prima di cambiare la selezione OTA. L’autenticazione vale per la sessione di manutenzione e dipende dal possesso della chiave mostrata a schermo. Questo profilo non abilita una firma di release permanente, Secure Boot o flash encryption.

Il display mostra l’avanzamento dei byte scritti. **Annulla OTA** è disponibile fino alla selezione dello slot. Senza upload l’AP scade dopo 120 s. Il trasferimento ha 90 s complessivi e 2 s di attesa per la lettura HTTP. Timeout, annullamento o upload incompleto lasciano selezionata l’app corrente. All’inizio di un upload valido, `esp_ota_begin()` può cancellare il settore otadata dell’app inattiva e il trasferimento può aver iniziato a sovrascrivere quello slot. Il record attivo conserva la selezione. L’AP si chiude e il radar riprende il gioco sul canale precedente. Una risposta HTTP positiva indica immagine verificata e nuovo boot selezionato. Lo stato VALID si verifica dopo il riavvio.

## Compilazione offline

Usare Arduino CLI con core ESP32 3.3.12 e le librerie del radar già installate.

```sh
RADAR_UI_TRIAL=1 RADAR_OTA_VERSION=1 scripts/build_linux.sh NUOVA_DIRECTORY_V1
RADAR_UI_TRIAL=1 RADAR_OTA_VERSION=2 scripts/build_linux.sh NUOVA_DIRECTORY_V2
```

Per l'OTA usare solo il file `dragon_radar_sketch.ino.bin` dalla directory `artifacts`. I profili senza wrapper NVS e il profilo DEV non emettono un descrittore M3f valido. Dimensioni e hash dei candidati sono in [STATUS.md](STATUS.md).

## Profilo flash verificato offline

| Regione | Offset | Dimensione |
|---|---:|---:|
| Bootloader ESP32-S3 | `0x0000` | regione fino a `0x8000` |
| Tabella partizioni | `0x8000` | `0x1000` |
| NVS | `0x9000` | `0x5000` |
| otadata | `0xe000` | `0x2000` |
| OTA0 | `0x10000` | `0x300000` |
| OTA1 | `0x310000` | `0x300000` |
| FAT | `0x610000` | `0x9e0000` |
| Coredump | `0xff0000` | `0x10000` |

I due dump originali da 16 MiB coincidono con l’hash storico. La cattura ROM precedente al collaudo cumulativo M3e ha lo stesso loader e la stessa tabella: OTA0 seq 3 VALID, OTA1 seq 4 VALID, selezione OTA1. Lo stato corrente va ricontrollato in ROM prima di ogni migrazione.

Il bootloader archiviato è ESP-IDF 5.5.5. Il suo binario coincide con il loader generato dal profilo Arduino 3.3.12 già usato per il radar. Gli header dichiarano DIO, 80 MHz e 16 MiB. L’app collega il profilo `qio_opi` con QIO per la flash e PSRAM ottale, già usato nel firmware cumulativo. Il disassemblato storico mostra il ramo NEW → PENDING_VERIFY e quello PENDING_VERIFY → ABORTED. La build SDK ha `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`; il servizio confronta in lettura le impronte di loader e tabella e controlla PSRAM e protezioni prima di aprire l’AP. [Formato immagine ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-reference/system/app_image_format.html)

M3f imposta `verifyRollbackLater()` per rinviare la conferma automatica di Arduino. In PENDING_VERIFY avvia un timer di 25 s dall’ingresso nell’inizializzazione Arduino. Conferma dopo NVS, display, touch, PSRAM e avvio locale ESP-NOW con associazioni DRA1 accessibili. Una sfera fisica presente o Paired non è necessaria. Errore di autoverifica richiede il rollback e il riavvio; se l’app si riavvia prima della conferma, interviene il loader al boot successivo. Un blocco tale da impedire l’esecuzione del timer può richiedere il watchdog di sistema o un reset fisico. La build M3f include la protezione contro cancellazione completa della NVS all’avvio. Associazioni e impostazioni restano nello stesso layout. [OTA e rollback Espressif](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-reference/system/ota.html)

## Protezione di OTA0 e migrazione autorizzabile

Con M3f attiva in OTA1, OTA0 sarebbe la destinazione del primo aggiornamento. Finché OTA0 contiene la baseline originale, il servizio mostra **OTA0 ORIGINALE PROTETTA** e blocca l’attivazione. Si può installare M3f in OTA1 via ROM, collaudarne gioco e avvio e fermarsi qui conservando tutta OTA0 sul radar.

Per abilitare entrambi gli slot occorre accettare che il primo OTA sostituisca l’originale in flash. La copia originale resta recuperabile dai backup integrali verificati. La migrazione si autorizza separatamente e richiede questo ordine:

1. A radar identificato in ROM, alimentazione e cablaggio da banco verificati, raccogliere due letture indipendenti e identiche dell’intera flash da 16 MiB. Conservare le due copie fuori da Git. Ricontrollare layout, protezioni, SHA-256, OTA0 originale e selezione OTA1.
2. Preparare gli artefatti in una nuova directory privata con `python3 scripts/prepare_radar_ota.py ORIGINALE_A ORIGINALE_B CORRENTE_A CORRENTE_B NUOVA_DIRECTORY`. Verificare `SHA256SUMS`. Lo script richiede entrambi i record correnti VALID, confronta loader/tabella e preserva le copie full-slot.
3. Con autorizzazione alla sola app, installare M3f v1 in OTA1 a `0x310000` e verificare integralmente il readback. Mantenere l’otadata corrente selezionata su OTA1. Verificare avvio, display, touch, radio e blocco di OTA0.
4. Con autorizzazione distinta, usare in ROM il solo settore `release-0x30f000.bin` all’offset `0x30f000`, ultimo settore dello slot OTA0 e oltre la fine dell’immagine originale nell’archivio. È una scrittura persistente in OTA0. Il file mantiene gli altri byte del settore. Rileggere e confrontare il settore byte per byte prima di riavviare. Lo stesso settore è escluso dal limite di tutti i futuri upload (`0x2ff000` byte), così l’abilitazione sopravvive anche a un upload interrotto.
5. Avviare v1, verificare AP, annullamento, timeout e ripresa. Aggiornare v1 → v2 nell’OTA0 inattivo solo con nuova autorizzazione. Verificare OTA0 VALID e backup OTA1. Alla release successiva ripetere la verifica in senso inverso. L’OTA scrive solo nell’app inattiva e nell’otadata gestita dal loader/SDK. Nella prima direzione il record inattivo si trova a `0xe000`. L’SDK può cancellarlo all’inizio e riscriverlo dopo la validazione per selezionare il nuovo boot. Verificare gli stati dei due settori dopo ogni prova.

In tutti i comandi ROM usare esclusivamente la porta riconfermata del radar. Esempio per il settore di rilascio, da eseguire soltanto dopo l’autorizzazione corrispondente e il controllo degli hash:

```sh
esptool --chip esp32s3 --port PORTA_RADAR --before no-reset --after no-reset write-flash \
  --flash-mode keep --flash-freq keep --flash-size keep 0x30f000 release-0x30f000.bin
```

## Recupero e collaudo

- **Ripristino del firmware originale.** Con autorizzazione separata, scrivere in ROM tutto `original-0x10000.bin` a `0x10000`, quindi leggere e confrontare l’intero slot prima di cambiare avvio. Scrivere `original-0xe000.bin` a `0xe000` solo dopo il readback riuscito, verificare l’intera otadata (rende OTA0 la selezione stabile), poi avviare. La scrittura full-slot ripristina anche il settore di rilascio. Nessuna scrittura di NVS, FAT, tabella, loader o eFuse.
- **Ritorno al banco precedente a M3f.** Se anche OTA1 è stata aggiornata, ripristinare `baseline-0x310000.bin` con la verifica full-slot, quindi `baseline-0xe000.bin` per riselezionare OTA1. Se si vuole riportare *l’intera coppia* di app allo stato prima di M3f, ripristinare e verificare anche `original-0x10000.bin` prima dell’otadata. Stesse opzioni ROM dell’esempio, cambia solo la coppia offset/file autorizzata.
- **Prova da banco.** Verificare nessun AP al boot, avvio su richiesta, password, annulla e timeout, rifiuto per firma/target/versione/immagine alterati, upload funzionante in entrambi i sensi, attività DRA1 dopo chiusura e stato VALID dopo avvio. Per il rollback fisico predisporre una variante privata che fallisca l’autoverifica e osservarne l’esecuzione solo previa autorizzazione. Le prove di reset e perdita di alimentazione durante il trasferimento o PENDING_VERIFY sono sessioni separate. Confrontare a livello logico DRC1/DRC2 e impostazioni, tenendo conto delle normali attività PHY. C6 resta in v332.

### Sequenza delle prove wireless

| Prova autorizzata singolarmente | App in esecuzione | Destinazione inattiva | Riscontro atteso |
|---|---|---|---|
| Prima release | v1 in OTA1, seq 4 VALID | v2 in OTA0 a `0x10000` | v2 seq 5 VALID in `0xe000`, v1 e record seq 4 preservati |
| Direzione inversa | v2 in OTA0, seq 5 VALID | v3 in OTA1 a `0x310000` | v3 seq 6 VALID in `0xf000`, v2 e record seq 5 preservati |
| Rollback intenzionale | v3 in OTA1, seq 6 VALID | fixture privata v4 in OTA0 | v4 seq 7 INVALID in `0xe000`, ritorno automatico a v3 seq 6 VALID |

Queste sequenze valgono a partire dai record 3 e 4 descritti sopra e in assenza di altri cambi di selezione. Prima di ogni invio riconfermare slot, sequenza, stato VALID, profilo flash e hash dell’app effettiva. Preparare e verificare una build UI Trial v3 con lo stesso core, layout e descrittore M3f per la direzione inversa. La fixture v4 dovrà rifiutare deliberatamente **solo** l’autoverifica del proprio primo boot in PENDING_VERIFY, prima di qualsiasi conferma. Firmare la fixture con il manifest della singola sessione dopo autorizzazione dedicata.

Dopo ciascuna prova verificare readback dell’app, entrambi i record OTA, confronto logico NVS e associazioni, ripresa DRA1 e settore di rilascio `0x30f000` byte per byte. Per v2 i 432 settori dell’app terminano a `0x1bffff`. Il limite in codice arrotonda sempre entro l’area che precede il settore di rilascio. Dopo rollback, il monitor e la lettura ROM devono confermare l’esecuzione di v3, non soltanto una risposta HTTP positiva o la presenza di due slot.

### Stato al termine della prima installazione

La v1 è stata scritta in OTA1 a `0x310000` e riletta byte per byte. I due nuovi dump da 16 MiB e tutti i file di recupero sono verificati e conservati in locale. Il controllo delle regioni esterne alla scrittura ha confermato OTA0 originale, otadata, NVS, FAT, loader, tabella e coredump preservati. Il settore di rilascio è ancora quello originale.

Riprendere con un avvio normale e il monitor USB del radar. Verificare PSRAM, separazione da AP, DRA1 con la sfera operativa e confronto NVS. Dal launcher aprire **Debug → Aggiornamenti OTA** (pagina 15/15). In **Sfera** selezionare ID 1 e leggere la versione. In **Radar** verificare **Attiva OTA → Conferma**, il messaggio **OTA0 ORIGINALE PROTETTA** e modalità Wi-Fi STA ancora attiva, con AP assente. Registrare separatamente il riscontro dell’operatore su touch e leggibilità. L’autorizzazione alla scrittura del settore di rilascio arriva dopo queste prove.
