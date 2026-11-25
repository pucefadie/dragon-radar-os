# M3e — OTA locale delle sfere

## Uso ordinario

L'app C6 avvia STA ed ESP-NOW sul canale configurato. L'AP si apre solo su comando USB `ota_start` seguito da invio, oppure da **Debug → Aggiornamenti OTA** sul radar. Scegliere ID, toccare **Attiva OTA** e confermare. Il radar attende l'ACK autenticato di preparazione prima di inviare il comando di ingresso. Il C6 mantiene attivo DRA1 per la finestra dei retry della risposta, spegne Grove e RGB e sospende il servizio di gioco per quella sfera. Il radar indica l'ACK che ha ricevuto. Il PC verifica separatamente la presenza dell'AP, l'esito dell'upload e il riavvio.

L'SSID è `DragonBall-01` per ID 1, fino a `DragonBall-07`. La chiave WPA e la chiave HMAC di release sono derivate con domini distinti dal segreto della singola sfera. Il PC usa la sua copia privata del **record sfera**, DRC1 o DRC2, in un file 0600. Nessuna nuova credenziale persistente viene scritta. Un PC che possiede il record può firmare release per quella sfera e va trattato come autorità di manutenzione. Conoscere soltanto la password dell'AP non consente di firmare una release. Questo profilo non abilita Secure Boot o flash encryption.

```sh
scripts/sphere_ota.py /percorso/privato/sphere.bin credentials
scripts/sphere_ota.py /percorso/privato/sphere.bin upload /percorso/app-c6.bin 302
```

Connettere il PC all'AP locale prima di eseguire `upload`. Rimanere sulla rete anche se il sistema operativo segnala assenza di Internet. Per il browser, generare un manifest privato e aprire `http://192.168.4.1/`. Selezionare manifest e binario. La barra riflette i byte inviati dal browser. La risposta positiva indica immagine verificata e slot selezionato, da distinguere dall'esito del primo boot.

```sh
scripts/sphere_ota.py /percorso/privato/sphere.bin manifest /percorso/app-c6.bin 302 /percorso/privato/release.json
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

Compilare l'app con Arduino ESP32 3.3.12 tramite `SPHERE_OTA_VERSION=301 scripts/build_m1c_c6.sh ota build/c6-301 1`. L'ID è l'ultimo argomento. Il CSV locale dello sketch è la tabella effettiva. Compilare separatamente il bootloader con ESP-IDF 5.5.4 già attivato tramite `scripts/build_sphere_ota_bootloader.sh build/c6-rollback-loader`. Verificare che le due tabelle generate coincidano. Il loader dedicato è compilato con rollback abilitato. L'app configura un termine di 10 s durante lo startup in stato PENDING_VERIFY. Conferma quando la configurazione locale, ESP-NOW, il peer e l'infrastruttura RGB/OTA sono inizializzati. Il pairing con il radar è indipendente dalla verifica. Un errore rilevato richiede il rollback; un reset prima della conferma permette al loader di marcare ABORTED e ricaricare l'app precedente. La prova di post-reset sul C6 rimane da eseguire. [Espressif OTA e rollback](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32c6/api-reference/system/ota.html)

## Migrazione USB una tantum

1. Dopo autorizzazione, mettere il solo XIAO C6 in ROM via USB, Grove e batteria scollegati. Identificare dispositivo e capacità di 4 MiB, verificare protezioni e porta. Acquisire due dump completi indipendenti e verificare i rispettivi SHA-256 e l'identità byte per byte. Conservare gli originali in locale.
2. Eseguire `scripts/prepare_sphere_ota_migration.py dump-a.bin dump-b.bin partition-table.bin bootloader.bin app-301.bin directory-privata-nuova`. Il controllo richiede il layout no_ota storico, selezione OTA0 valida e SPIFFS vuota. Per SPIFFS con dati, predisporre una migrazione dati prima di scrivere. Il controllo sul vecchio dump è di fattibilità. Al banco servono due nuovi dump.
3. Con autorizzazione esplicita alle tre scritture, usare esptool in ROM con reset finale disabilitato. Scrivere solo bootloader, tabella e OTA0. Usare gli artefatti privati verificati e la porta riconfermata. NVS, otadata e coredump restano fuori dall'operazione. L'area OTA1 va popolata dal successivo upload firmato.

   ```sh
   esptool --chip esp32c6 --port PORTA_C6 --before no-reset --after no-reset write-flash \
     --flash-mode keep --flash-size keep --flash-freq keep \
     0x0 migration-0x0000.bin 0x8000 migration-0x8000.bin 0x10000 migration-0x10000.bin
   ```

4. Rileggere e confrontare tutti i byte scritti e verificare NVS e otadata identiche prima del primo avvio. Dopo il boot, leggere versione e stato e accertare che l'AP resti spento finché non richiesto.
5. Per tornare al layout precedente, entrare in ROM. Con autorizzazione di ripristino, verificare gli hash e riscrivere i quattro file `restore-0x0000.bin`, `restore-0x8000.bin`, `restore-0xe000.bin` e `restore-0x10000.bin` ai rispettivi indirizzi, con gli stessi parametri e senza reset finale. La regione da `0x10000` ricopre vecchia app, vecchia SPIFFS e possibili scritture del nuovo OTA1. Verificare ognuna prima di avviare. NVS e coredump restano fuori dal ripristino. I dump completi sono conservati per recupero ROM separato.

## Collaudo

- **Solo C6** dopo migrazione autorizzata, verificare assenza AP all'avvio, `ota_start`, WPA, timeout senza upload, firma/ID/versione/chip errati, upload interrotto, 301 → 302, primo boot, stato OTA, DRC1 e luminosità persistenti. Verificare il ritorno a OTA0 con una release di banco firmata con autoverifica deliberatamente fallita. Verificare separatamente reset o perdita alimentazione durante ricezione e prima della conferma del boot.
- **C6 + S3** verificare pagina Debug, query versione, conferma e indirizzamento della richiesta, compatibilità con C6 precedenti, ripresa DRA1 e funzioni RGB. La verifica che le altre sfere restino operative richiede più di una sfera fisica.
- Le misure RGB/Grove, il GPS sul campo e il collaudo multisfera fisica restano prove separate.
