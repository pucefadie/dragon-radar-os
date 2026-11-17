# Stato snapshot pulito Dragon Radar OS v0.3

Branch di lavoro: `feat/espnow-m1-bidirectional`, partito da `main` aggiornato (`b0483cd`).
Checkpoint pubblicato: `5ce4d48271594618733ecc74499696acc09241dd`; PR draft #2 aperta verso `main`: https://github.com/pucefadie/dragon-radar-os/pull/2. Non fare merge: la Milestone 1 è dichiarata incompleta nella PR.

Questo repository è una copia Git indipendente, senza la cronologia locale diagnostica. La cronologia completa e i backup restano nella directory privata locale originale.

## Stato software

- Build S3 dalla directory pulita: riuscita con Arduino ESP32 Core 3.3.12; 1.768.859 byte app su 3.145.728, 71.100 byte globali.
- Build C6 dalla directory pulita: riuscita con Arduino ESP32 Core 3.x; 961.914 byte app su 1.310.720, 41.616 byte globali.
- DRB2 broadcast resta compatibile; i frame diretti usano flag DIRECTED e campi riutilizzati per destinazione/payload length. ID controller 0, sfere 1–7, broadcast 255.
- S3 riceve beacon e registra MAC/RSSI; esempio XIAO invia beacon ogni ~500 ms con jitter ±30 ms.
- Moduli host `PairingRegistry`, `ReplayWindow`, `RadioAckTracker` e test per pairing window, replay, ACK errato/corretto, retry/timeout e indirizzamento passano con `-Wall -Wextra -Werror`.
- Le macchine host non sono ancora integrate nel trasporto. Pairing/autenticazione, NVS, LMK e cifratura unicast, ACK/retry effettivi e heartbeat end-to-end restano incompleti. Non dichiarare la milestone completata.
- Build finale S3 riuscita: app 1.768.887 / 3.145.728 byte, globali 71.100 byte; SHA-256 `adb44d5b538925452c02c6098d734664305fd81581deef6247a07637205795af`.
- Build finale C6 riuscita: app 961.976 / 1.310.720 byte, globali 41.616 byte; SHA-256 `87128d6d3552aec60eb0df659023d735a0195a5cd144079d227c084130b1b965`.
- Test host `beacon_protocol_test`, `radio_link_test`, `beacon_registry_test`: passati con `-Wall -Wextra -Werror`.
- La Milestone 1 NON è completata: le state machine host non sono collegate al trasporto, manca TX/RX applicativo bidirezionale, pairing fisico, persistenza NVS, provisioning protetto LMK, cifratura ESP-NOW, heartbeat/ACK end-to-end e recovery canale AP. Nessun test radio fisico o flash è stato eseguito.

## Avvio lavoro

```bash
./scripts/build.sh full
./scripts/build_beacon.sh 1 4 esp32:esp32:XIAO_ESP32C6
```

Per la radio, verificare che entrambe le schede usino lo stesso canale e che l'antenna U.FL sia collegata. Nessun RSSI indica distanza o bearing. Prima del collaudo unicast implementare pairing e chiavi; CRC e MAC non autenticano il mittente.
