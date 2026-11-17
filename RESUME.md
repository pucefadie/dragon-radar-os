# Stato snapshot pulito Dragon Radar OS v0.3

Questo repository è una copia Git indipendente, senza la cronologia locale diagnostica. La cronologia completa e i backup restano nella directory privata locale originale.

## Stato software

- Build S3 dalla directory pulita: riuscita con Arduino ESP32 Core 3.3.12; 1.768.859 byte app su 3.145.728, 71.100 byte globali.
- Build C6 dalla directory pulita: riuscita con Arduino ESP32 Core 3.x; 961.914 byte app su 1.310.720, 41.616 byte globali.
- Protocollo locale v0.3: frame binario con CRC e enum messaggi; gli sketch radio del pacchetto di integrazione sono da valutare prima di sostituire il framing già presente.
- S3 riceve beacon ESP-NOW con MAC/RSSI; esempio XIAO trasmette beacon.
- Test host presenti per frame, registro beacon e prossimità.
- Pairing autenticato, provisioning LMK, cifratura unicast, ACK/retry end-to-end e telemetria batteria restano incompleti.
- Test host frame/CRC, registro, prossimità e matematica radar: passati. Nessun test radio fisico o flash è stato eseguito in questo snapshot.

## Avvio lavoro

```bash
./scripts/build.sh full
./scripts/build_beacon.sh 1 4 esp32:esp32:XIAO_ESP32C6
```

Per la radio, verificare che entrambe le schede usino lo stesso canale e che l'antenna U.FL sia collegata. Nessun RSSI indica distanza o bearing. Prima del collaudo unicast implementare pairing e chiavi; CRC e MAC non autenticano il mittente.
