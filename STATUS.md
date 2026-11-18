# Dragon Radar OS — stato condiviso ChatGPT ↔ Codex

**Ultima ricognizione ChatGPT: 2026-11-18.**
**Fonte remota:** repository `pucefadie/dragon-radar-os`, branch `feat/espnow-m1-bidirectional`, PR #2 draft (head osservato `062a78da514ea3bba5ba7b8a06ffd8c83498ddcf`). La cronologia locale Ubuntu, i dump e gli artefatti `build/` non sono accessibili dalla sola copia GitHub.

## Stato verificabile dai documenti GitHub
- `main` contiene snapshot pulito v0.3; `RESUME.md`, `PROTOCOL_AND_POWER.md`, `FIRST_RADIO_TEST.md` descrivono protocollo e prove.
- PR #2 **aperta, draft, non mergiata**, 10 commit al momento della revisione, 24 file modificati.
- PASS **dichiarati dalla PR**, non rieseguiti da ChatGPT: test host `dev_radio_test`, `dev_radio_tx_test`, codec, registry e radio_link; build S3/C6 default e DEV. S3 DEV 1.776.871 byte / 71.244 globali; C6 DEV 967.514 byte / 41.688 globali. Questi numeri sono checkpoint, non garanzia per commit futuri.
- Lato DEV: sfera ID 1, trasporto in chiaro non autenticato, discovery/STATUS/PING/PONG/COMMAND 0/ACK/HEARTBEAT simulati; gestione TX concorrente e timeout driver testata con mock.
- NOT RUN: flash/prova ESP-NOW reale S3↔C6; recupero automatico canale; pairing autenticato, NVS/LMK e cifratura end-to-end non completati. Nessun attuatore fisico deve essere azionato via DEV.
- Evidenze storiche locali riportate dall'utente: diagnostica display, bussola, motore, pulsante; non equivalgono a collaudo del firmware attuale.

## Vincoli di prodotto correnti
- Radar S3 Waveshare AMOLED con GPS LC76G e BMM150; sfera XIAO ESP32-C6 con IMR14500 500 mAh, motore Grove con driver, 3 WS2812B, senza buzzer.
- Due modalità Aperto (GPS + ESP-NOW) e Chiuso (solo ESP-NOW); RSSI per prossimità grossolana, non metri/direzione; sotto ~30 cm oggetto considerato trovato visivamente.
- Prototipo di **una** sfera. Sicurezza batteria PCM 1S e cablaggio non ancora validati: nessuna prova a batteria non protetta.

## Prossimo lavoro autonomo consentito
1. Controllare `git status`, branch, diff verso PR #2, `RESUME.md`; NON assumere che lo stato locale sia uguale al remoto.
2. Rafforzare e testare offline la separazione dei canali DEV non autenticato vs produzione; progettare/incrementare handshake e test pairing, NVS, LMK e anti-replay senza eseguire flash.
3. Rilanciare i test host e le build S3/C6 default + DEV quando disponibili; aggiornare questa sezione con SHA, comandi, esiti esatti e regressioni.

## STOP / decisioni richieste all'utente
- Prima di collegamento reale o flash: piano dettagliato target, porte, partizioni, rollback, stato protezione cella e autorizzazione esplicita.
- Prima di merge verso `main`: revisione e approvazione esplicita della PR.
- Non usare risultati mock per dichiarare M1 conclusa.

## Checkpoint M1b — implementazione offline (2026-11-18)

**Branch/base remota:** `feat/espnow-m1-bidirectional`, base `6a210e2`. PR #2 draft. Committi locali recuperati senza forzature; modifiche di banco tenute separate. Revisione AI #3 `6988679373` letta.

**Attività appena conclusa:** DRA1 standard per una sfera (ID 1), peer ESP-NOW cifrato, handshake HMAC a doppia sfida, anti-replay, ACK diagnostico e retry, schema NVS versionato, generatore offline e utility di provisioning cablata. DEV separato, coda driver a un solo invio in volo, aggiunta peer limitata e recupero da callback tardiva. Vettori binari DRB2 broadcast/directed.

**Test PASS:** 8 suite host con `g++ -std=c++11 -Wall -Wextra -Werror` (README). Inclusi handshake, retry, collisioni/destinazione, replay e riavvio, compatibilità e code.

**Test NOT RUN:** collaudo fisico bidirezionale e provisioning su scheda. Come evidenza storica distinta, il ricevitore minimale separato ha ricevuto 10 DRB2 validi confermati sul canale 6 e l'intera NVS era identica dopo il test. Quella prova non verifica DRA1.

**Build:** S3 e C6 standard/DEV e utility di provisioning in compilazione offline con Arduino ESP32 3.3.12. I risultati di questa revisione saranno registrati nel checkpoint di verifica.

**Hardware toccato in questo ciclo:** NO. **Push:** da eseguire dopo verifica delle build, senza forzature.

**Blocchi reali:** nessuno per continuare la verifica software. Cifratura a riposo, rotazione/revoca e collaudo fisico non validati; vincolo del singolo peer e canale esplicito.

**Prossime tre azioni:** finalizzare compilazioni e controlli di build; rivedere protezione a riposo e procedura di provisioning/rollback prima del banco; collaudare handshake cifrato, perdita/recovery e retry con autorizzazione separata.
