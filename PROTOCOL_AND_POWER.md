# Protocollo radio e alimentazione

DRB2 mantiene il frame broadcast da 34 byte, little-endian e CRC16. Nei frame diretti, il flag DIRECTED riusa il byte reserved come payload length e il precedente payload-length byte come destinazione; i beacon broadcast restano compatibili. ID radar 0, sfere 1–7, broadcast 255. Il codec verifica tipo, ID, flags, dimensione e CRC.

Il collegamento M1b usa una sfera prototipo, ID 1. M3 multisfera estende il supporto fino a sette sfere. I beacon DRB2 restano pubblici e non autenticati. Il traffico diagnostico sicuro usa un formato separato DRA1 da 80 byte. Contiene ruolo e ID di origine/destinazione, due nonce da 128 bit, sequenza o riferimento ACK e HMAC-SHA-256. ESP-NOW usa un peer unicast con LMK e PMK configurate; l'app verifica anche HMAC, indirizzi e sessione. I nonce sono generati quando il Wi-Fi è attivo. Non si ricava l'autenticazione dal CRC, dal MAC sorgente o da un flag dichiarato dal peer.

Il radar sfida la sfera e verifica la risposta che lega entrambe le sfide alla chiave. La sfera accetta FINISH solo per una sfida fresca. Il radar apre la sessione dopo READY. I probe di stato hanno sequenza crescente e ACK correlato; il duplicato dell'ultima richiesta può riottenere l'ACK senza aggiornare la validità della sessione. Un riavvio richiede un nuovo handshake. La modalità DEV rimane esclusa dalla build standard e non condivide la macchina di autenticazione. Da M3b il profilo autenticato supporta anche il comando di vibrazione descritto qui sotto.

I beacon C6 sono schedulati circa ogni 500 ms con jitter ±30 ms. Entrambi i peer richiedono il canale registrato. Il C6 resta sul canale impostato in build. Il radar legge il canale effettivo senza modificare quello imposto dall'AP. Fuori canale, il collegamento sicuro non elabora messaggi e scade. Il recupero riparte tramite handshake quando i canali tornano allineati; la ricerca automatica del canale resta da implementare. In assenza della callback TX l'adapter sospende gli invii fino a callback tardiva o riavvio, senza attribuire un completamento al frame successivo.

### M3b — vibrazione singola da Debug

DRA1 conserva il frame da 80 byte, la coppia di nonce, il peer unicast cifrato e HMAC. Il tipo 7, `Vibrate`, usa una sequenza propria del comando e riferimento ACK zero. Il tipo 8, `VibrateResult`, riporta il riferimento alla richiesta nel campo ACK e il risultato nel campo sequenza. I risultati ammessi sono 2 (accettato dal driver), 3 (cooldown o driver occupato) e 4 (indisponibile). Il comando prevede un unico impulso da 120 ms, con durata e intensità fisse.

Una richiesta parte solo da una sessione Paired. Il controller prova fino a tre invii della stessa richiesta nella finestra di 1,35 s. La sfera accetta solo una sequenza nuova nella sessione corrente. Un retry dell'ultima richiesta restituisce il risultato memorizzato senza chiamare di nuovo il driver. Il comando usa il cooldown condiviso con l'impulso di accensione e il test USB. Il relativo risultato non è un ACK di probe e non alimenta il filtro RSSI. Se manca la risposta, l'esito resta non confermato. La conferma indica che il driver ha accettato l'impulso e lascia all'operatore la verifica della vibrazione fisica.

### Esito M1c e scelta M1d

Sul banco Arduino ESP32 3.3.12 con PMK locali distinte, lo S3 ha registrato 69 esiti TX MAC positivi e il C6 zero callback DRA1. Con una PMK comune derivata in RAM, sugli stessi dispositivi e con gli stessi record NVS, sono state osservate 20 nuove conferme per dispositivo in 60 s. Sono passati HMAC alterato, replay, ACK/retry e recupero. La seconda immagine correggeva anche la diagnostica EUI-64 del C6. Il primo profilo radio, privo di quella diagnostica, era già fallito. Questi dati localizzano la differenza osservata prima del decoder DRA1. Il meccanismo interno del driver e il bit Protected/CCMP sul canale non sono stati verificati da uno sniffer.

Espressif espone una PMK per istanza ESP-NOW e una LMK per peer. L'API descrive la PMK come protezione delle LMK e la LMK come chiave CCMP del peer ricevente e trasmittente. La documentazione richiede la stessa LMK e non dichiara che PMK diverse debbano interoperare su ogni combinazione di target. L'esempio ufficiale carica la PMK dalla configurazione prima di registrare i peer. [Guida ESP-NOW ESP-IDF 5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c6/api-reference/network/esp_now.html) · [Esempio Espressif v5.5.4](https://github.com/espressif/esp-idf/blob/v5.5.4/examples/wifi/espnow/main/espnow_example_main.c)

### Record e provisioning USB

| Record | Chiave NVS in `dragon_radio` | PMK effettiva |
|---|---|---|
| DRC1 preesistente | `peer_v1` | Primi 16 byte di HMAC-SHA-256 della chiave applicativa sul contesto fisso M1c. La derivazione avviene in RAM ed è compatibile con le immagini del banco riuscito. |
| DRC2 nuovo | `peer_v2` | 16 byte casuali condivisi nel dominio radar–sfere e distribuiti via cavo. |

Entrambi i record sono di 76 byte. L'ordine è magic da 4 byte, ID, canale, MAC STA del peer da 6 byte, LMK da 16 byte, chiave HMAC da 32 byte, campo PMK da 16 byte. DRC2 mantiene LMK e HMAC distinte per la coppia. Il generatore usa tre estrazioni casuali separate e assegna la stessa PMK ai due record. Non c'è una PMK compilata nel firmware. La PMK è per dominio perché `esp_now_set_pmk()` non accetta un peer. M3 multisfera usa una PMK installata una volta sul radar e una configurazione per ID con MAC, LMK e HMAC di ogni coppia. La PMK condivisa va trattata come materiale del dominio. La compromissione di una sfera richiede una politica di revoca e rotazione, senza riuso delle chiavi della coppia revocata. Il limite configurato nel core del radar è sette peer cifrati.

Il loader apre NVS in sola lettura. Se `peer_v2` esiste, verifica tipo, lunghezza, magic, ID, canale, peer e chiavi. Un DRC2 presente ma invalido blocca DRA1. Il solo caso che consente di leggere DRC1 è l'assenza di `peer_v2`. Errori di derivazione o di installazione nel driver impediscono l'avvio del link. I buffer di caricamento e derivazione vengono azzerati. Serial riporta la versione del record e gli esiti numerici delle API, senza stampare chiavi o MAC. L'opzione sperimentale M1c è rimossa. DRA1 e la finestra anti-replay conservano lo stesso formato e gli stessi controlli.

`python3 scripts/make_radio_records.py <directory-privata-esterna-al-repo> --channel 6` genera una nuova coppia DRC2 con i MAC STA inseriti su stdin. I file sono a permessi 0600. L'utility USB separata accetta esclusivamente `P` seguito da un DRC2. Crea `peer_v2` solo se la chiave è assente; lascia `peer_v1` intatto e rifiuta qualunque sostituzione, anche con un valore esistente malformato. Inviare i file su una connessione USB locale fidata senza echo o log dei payload. Un errore di scrittura richiede una verifica in lettura prima di riprovare. Una transizione DRC1→DRC2 va coordinata su entrambe le schede. Durante il passaggio da un solo lato sono attese PMK effettive diverse e l'handshake potrebbe non proseguire.

Il ripristino delle immagini M1c riporta la coppia al percorso DRC1 senza modificare NVS, se i due record precedenti sono ancora presenti. Riavviare M1d riattiva la precedenza di DRC2. Il ritorno di M1d a DRC1 dopo aver salvato DRC2 richiede un'operazione di manutenzione distinta. La sostituzione di un DRC2 già presente richiederà uno strumento di rotazione che verifichi il record atteso e predisponga l'attivazione su entrambi i dispositivi. Non fa parte di questa utility. Con flash/NVS in chiaro la lettura fisica può esporre le chiavi. Vedere [SECURITY_PLAN.md](SECURITY_PLAN.md) per la protezione a riposo.

L'antenna esterna XIAO viene selezionata prima di avviare la radio: GPIO3 LOW, GPIO14 HIGH. Collegare l'antenna U.FL. Attuatori, ADC batteria, shake sensor e deep sleep non sono implementati; non pilotare carichi direttamente dai GPIO. Non è stata misurata autonomia.


## M3 multisfera — aggiungere un'associazione via USB

Ogni sfera ha un ID univoco da 1 a 7. Il radar conserva sette sessioni DRA1 con nonce, replay, comandi e misure indipendenti. Sorgente e destinazione includono l'ID secondo il formato DRA1 esistente. ID 1 mantiene i record DRC1 e DRC2 e lo stesso formato radio. Il C6 M3b è stato collaudato fisicamente con DRC1. Il radar considera per prime le credenziali della sfera 1, se presenti e valide. Il primo peer accettato determina canale e PMK. Record successivi con canale o PMK diversi o MAC già registrato non vengono attivati.

Per ogni nuova sfera, conservare fuori da Git una copia privata del record radar di ID 1 già installato. Per un dominio DRC1 il generatore ricava in locale la stessa PMK di M3b. Per DRC2 riprende la PMK del record. Per ogni coppia genera LMK e HMAC nuove.

1. Assegnare un ID libero. Preparare i due record. Il generatore richiede i MAC STA del radar e della nuova sfera. L'esempio aggiunge ID 2 al dominio sul canale 6.

   ```sh
   python3 scripts/make_radio_records.py "$HOME/privato/dragon-radar/sfera-2"       --id 2 --channel 6 --domain-record "$HOME/privato/dragon-radar/sfera-1/radar.bin"
   scripts/build_m1c_c6.sh radio build/c6-radio-id2 2
   ```

2. Dopo autorizzazione hardware e backup, caricare l'utility di provisioning USB in uno slot autorizzato. Per S3 usare `M1C_BENCH_PROVISION=1 scripts/build_m1c_bench.sh build/s3-provision`. Per C6 usare `scripts/build_m1c_c6.sh provision build/c6-provision`. Tenere batteria e Grove scollegati durante programmazione, ROM e provisioning.
3. A utility in esecuzione, inviare via USB un byte ASCII `P` seguito dai 76 byte di `radar.bin` al radar e di `sphere.bin` alla nuova sfera. Attendere `RADIO_PROVISION_OK` prima di inviare un altro record. I record aggiuntivi finiscono nelle chiavi `peer_v2_2`–`peer_v2_7`. L'utility rifiuta di sovrascrivere un valore esistente. Per estendere il dominio non inviare alcun nuovo record per ID 1.
4. Con autorizzazione di scrittura, avviare l'app S3 multisfera e installare l'app radio del nuovo C6 compilata per lo stesso ID. Il C6 ID 1 conserva M3b. In **Radar → Reale** verificare selezione, Paired e misura fresca, poi ripetere per un altro ID. Accendere progressivamente le altre sfere per il collaudo concorrente.
