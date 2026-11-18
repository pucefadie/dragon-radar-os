# M1c — collaudo su banco in preparazione

La preparazione è autorizzata. L'esecuzione, la programmazione, il provisioning e il ripristino richiedono un'autorizzazione distinta. Baseline `a43bfe7`, sorgenti firmware `7ff019d`. Vedere [STATUS.md](STATUS.md) per le verifiche e [SECURITY_PLAN.md](SECURITY_PLAN.md) per la decisione sulle chiavi.

## 1. Predisposizione fisica, a schede spente

1. Identificare il Waveshare ESP32-S3-Touch-AMOLED-1.75-G e il Seeed XIAO ESP32-C6. Apporre etichette **S3** e **C6** sui cavi USB dati e predisporre due terminali distinti. Tenere le schede a 1–2 m. Usare USB come unica fonte, senza collegare alcuna batteria.
2. Sul C6, agganciare l'antenna esterna U.FL prima di alimentare. Il binario standard del checkpoint seleziona GPIO3 LOW e GPIO14 HIGH. La precedente variante interna è un reperto storico. [Guida Seeed](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)
3. Isolare dall'alimentazione motore, LED esterni e altri attuatori. Sul S3 la demo locale può azionare il motore a seconda delle impostazioni, anche senza traffico radio. Prima di avviare il firmware standard, verificare che il motore sia fisicamente isolato oppure usare un'immagine di banco separata con azionamenti esclusi, da approvare e verificare. Se non si può garantire l'isolamento, fermarsi. Evitare debug, demo, audio e comandi `testall`/`vibe*`.
4. Preparare un AP 2,4 GHz dedicato al banco, configurato sul canale 6, e un secondo canale legale per il caso di disallineamento. Il radar segue il canale dell'AP associato; il C6 di questa build resta sul 6. Evitare modifiche alla rete domestica. Leggere il canale reale prima di generare i record. Se non è 6, riallineare l'AP dedicato o preparare una build e record per il nuovo canale.

## 2. Preflight futuro in sola lettura

Identificare un solo dispositivo per volta tramite enumerazione USB e risposta ROM. Le porte storiche sono COM11 per il radar, COM12 per la sfera e COM5 per una scheda estranea. Ricontrollare le porte dopo ogni riavvio o passaggio in ROM. I monitor app e ROM non devono contendersi la porta. Registrare mapping e indirizzi **STA** soltanto in locale. L'identità USB non sostituisce l'indirizzo STA.

Con la procedura di preflight autorizzata, usare esptool per `get-security-info`, `flash-id`, `read-flash` e `verify-flash`; verificare chip, revisione, capacità, protezioni, hash, tabella partizioni e record OTA. La lettura in ROM può richiedere un reset: entrare direttamente in ROM quando è importante non riavviare l'app prima delle letture. Usare BOOT durante reset/accensione se necessario, poi enumerare di nuovo. Confermare che il metodo sia ripetibile prima di approvare una scrittura. Verificare anche quale app risponde in modalità normale: selezione OTA e app effettivamente avviata sono evidenze distinte.

| Target | Evidenza storica da ricontrollare | Piano da usare solo se la lettura corrente coincide |
|---|---|---|
| S3, 16 MiB | NVS `0x9000/0x5000`, otadata `0xe000/0x2000`, OTA0 `0x10000/0x300000`, OTA1 `0x310000/0x300000`. Dopo il trial RX, OTA0 integro; OTA1 conteneva il ricevitore minimale ed era selezionato con sequenza 4 VALID. | Conservare OTA0. App di provisioning e poi app standard nel solo OTA1. Se OTA1 risulta ancora selezionato con record stabile, non riscrivere otadata. Se è selezionato OTA0, predisporre una selezione esplicita, verificata dal preflight corrente e autorizzata separatamente. |
| C6, 4 MiB | App0 `0x10000/0x200000`, NVS `0x9000/0x5000`, otadata `0xe000/0x2000`, SPIFFS `0x210000/0x1e0000`, coredump `0x3f0000/0x10000`. Un solo slot. L'ultimo firmware osservato era DEV con antenna interna. | Usare immagini con profilo `esp32:esp32:XIAO_ESP32C6:PartitionScheme=no_ota` confrontate con la tabella archiviata. La sostituzione di app0 richiede una copia integrale e corrente dello slot e un recupero ROM pronto. Gli artefatti C6 M1b a due slot non sono la scelta di banco. |

Per acquisire l'inventario, dopo aver validato le porte e con i terminali chiusi, eseguire `esptool --chip <esp32s3|esp32c6> --port <porta-verificata> get-security-info` e `flash-id`. Con `read-flash 0x0 0x9000 <file-privato>` acquisire bootloader/tabella; leggere a parte NVS, otadata e app dalle regioni della tabella sotto portata effettiva. I segnaposto non vanno riutilizzati come valori di porta. Se le letture lunghe si interrompono, usare blocchi con indirizzi e lunghezze verificati, ricomporre due copie e confrontarle.

Prima di proporre qualunque scrittura, ottenere due copie indipendenti e identiche dell'app che verrà sostituita, della NVS completa e di otadata. Verificare anche tabella, bootloader, integrità dell'app preservata e disponibilità del backup completo per ogni scheda. Il backup C6 storico precede l'ultima app installata e non è una copia dell'app corrente. Conservare tutto fuori Git in directory private. Annotare intervallo, dimensione e SHA-256 e confrontare l'intervallo di cancellazione arrotondato a 4 KiB con i confini dello slot. Se la console app manca, la tabella diverge, la ROM non è raggiungibile o l'identità è ambigua, fermarsi.

## 3. Provisioning e installazione — fasi da autorizzare

Prima della futura sessione autorizzata, predisporre una directory privata fuori dal repository. Dopo aver confermato gli indirizzi STA e il canale, generare una coppia di record nuova con `umask 077` e:

```bash
python3 scripts/make_radio_records.py "$HOME/.local/share/dragon-radar-os/records-m1c-sessione-nuova" --channel 6
```

Il percorso deve essere nuovo. Il generatore crea `radar.bin` e `sphere.bin` da 76 byte, con LMK e chiave HMAC condivise e PMK locale differente. Non stampare, inviare a terzi o conservare in log i payload. NVS in chiaro è accettata solo per questi segreti temporanei al banco. La generazione dei record reali e l'invio USB non fanno parte della preparazione odierna.

1. Approvare un piano per scheda con porta verificata, hash dell'app, offset, intervallo di erase implicito, app originale e recupero ROM. Usare solo il file app `.ino.bin`. I file merged, bootloader, partitions, immagini NVS e comandi generici di upload non rientrano nel piano.
2. Installare e rileggere l'utility cablata nel solo slot indicato. Ricontrollare i metadati OTA. Prima di inviare dati verificare che l'utility sia quella attesa. L'utility usa Arduino: l'avvio può modificare NVS oltre al record e, in condizioni di errore, il core può tentare il recupero della partizione. L'autorizzazione futura deve coprire il provisioning e il relativo rischio; se è richiesta NVS immutabile, l'utility attuale non soddisfa il requisito.
3. Con entrambi i monitor chiusi, inviare per ciascuna porta identificata, a 115200, **in una sola scrittura binaria** il byte ASCII `P` seguito dal file per quel ruolo. Non aggiungere newline. Attendere l'esito `RADIO_PROVISION_OK` entro il timeout. A `RADIO_PROVISION_REJECTED`, fermarsi e non cancellare il record esistente. L'utility non sovrascrive un'associazione già presente. Conservare solo l'esito nei log.
4. Rientrare in ROM, sostituire l'utility con l'app standard verificata nello stesso slot, rileggere e confrontare esattamente l'immagine. Solo dopo verificare la selezione e avviare normalmente. Registrare i canali e controllare che le due app partano con `configured=1`. Usare `beacons` sul radar; la sfera stampa lo stato ogni 5 s.

## 4. Matrice di prova futura

Osservare contemporaneamente entrambi i terminali con timestamp. `state=2` attende una sessione autenticata, `3` indica Paired, `4` Lost. `confirmed` è un contatore locale: sul radar cresce all'ACK applicativo accettato, sul C6 alla prima accettazione di un probe. L'esito TX del driver e i beacon DRB2 non provano l'autenticazione DRA1.

| Caso | Azione futura | Criterio |
|---|---|---|
| Handshake, canale e ACK | Tenere le schede a 1–2 m, canale effettivo 6, acquisire almeno 60 s | `configured=1 aligned=1 state=3` su entrambi; cresce `confirmed` su entrambi; registrare drop e `tx_stalled`. |
| Riavvio radar | Riavviare solo S3, C6 alimentato | Il radar rilegge l'associazione, apre una nuova sessione e riprende gli ACK. Registrare tempi e contatori prima/dopo. |
| Riavvio sfera | Riavviare solo C6, S3 alimentato | Il radar può mostrare Lost dopo la scadenza di circa 15 s; ritorno a Paired e probe confermati senza riprovisionare. |
| Perdita e ritorno | Scollegare USB C6 per almeno 20 s, riconnetterlo | Lost sul radar durante la perdita; nuovo handshake e ACK al ritorno. |
| Cambio e ripristino canale | Solo AP isolato: spostarlo su altro canale, confermare il nuovo canale S3, poi tornare a 6 | Sul S3 `aligned=0`, conferme ferme e scadenza; al ritorno `aligned=1` e recupero. Il C6 non cerca altri canali. |
| ACK perso e retry | Fixture dedicata che sopprime il primo ACK applicativo mantenendo attiva la sessione | Rinvio dello stesso probe dopo circa 450 ms, ACK finale, una sola accettazione sul C6. |
| Tag errato, duplicato e replay | Fixture autorizzata che invia tramite il proprio peer cifrato un tag DRA1 alterato, poi un duplicato della sessione e una richiesta vecchia dopo nuovo handshake | Nessuna conferma o refresh per tag errato o richiesta vecchia. Il duplicato corrente può ricevere l'ACK già prodotto, senza incrementare le accettazioni né rinnovare la sessione. |

I binari standard non espongono soppressione ACK, replay controllato, nonce, conteggio retry o attestazione indipendente del bit CCMP. I tre casi negativi sono verificabili su host; **il collaudo fisico mirato di autenticazione negativa, retry e replay resta NOT RUN finché non viene preparata e approvata una fixture di banco con questi punti di controllo**. Per la fixture futura usare fault deterministici di un solo evento sul dispositivo autorizzato, fra parsing e consegna applicativa. Tenere chiavi, nonce e frame completi fuori dai log. Esporre solo tipo prova, numero tentativo, esito accettato/rifiutato e incremento conferme. Acquisire il corrispondente campione valido prima di ogni prova negativa. Non dedurre tale copertura da reset, beacon o contatori aggregati. Se non compare Paired dopo 30 s sullo stesso canale, conservare il log e stop. Ripetere la prova solo dopo aver identificato la causa.

## 5. Chiusura e rollback

- Rientrare in ROM e confrontare bootloader, tabella, otadata, slot preservato e NVS prima/dopo. La NVS durante M1c è attesa **diversa** a causa del provisioning. L'audit NVS immutata del ricevitore minimale precedente non copre questa prova.
- S3: per tornare a OTA0 usare esclusivamente un record di selezione verificato e autorizzato per il bootloader originale; restaurare OTA1 solo se espressamente previsto. C6: ripristinare l'intera app0 catturata prima del banco; senza OTA1 il recupero è in ROM. Verificare in lettura e poi riconoscere la vecchia app al boot. Nessun ripristino integrale del chip.
- Il ripristino dell'app o della selezione non revoca i record NVS. Pianificare la rimozione mirata dei record di banco o, se espressamente autorizzato e verificato, il ripristino della NVS precedente. Non riutilizzare chiavi temporanee; eventuali vecchi dump restano dati sensibili. Non cancellare NVS per risolvere errori o rotazione.
- Esito da registrare per ogni riga: PASS/FAIL/NOT RUN, commit e hash, stato iniziale/finale, canali, timestamp, limiti e ritorno al firmware precedente. M1 resta aperta finché le prove richieste non sono documentate.
