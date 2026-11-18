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

## Protocollo di aggiornamento per Codex (sostituire questa sezione a ogni checkpoint)
**Branch/SHA:** [riportare reale]
**Attività appena conclusa:** [file/componenti]
**Test PASS:** [comando + esito]
**Test FAIL:** [comando + errore]
**Test NOT RUN:** [motivazione]
**Hardware toccato:** NO salvo via libera esplicito
**Push GitHub:** [riuscito/negato; SHA remoto]
**Blocchi reali:** [nessuno o descrivere]
**Prossime 3 azioni:** [ordinate]
**Intervento utente:** [solo se indispensabile]
