# Dragon Radar OS — roadmap eseguibile

Aggiornamento: 2026-11-18. Questa roadmap coordina Codex e revisione ChatGPT, non certifica test fisici.

## M0 — Baseline e sicurezza (preservata)
- Snapshot pulito v0.3 su `main` e ramo `feat/espnow-m1-bidirectional` per radio. Diagnostica/backups originali restano locali; mai sovrascriverli.
- Prima di ogni cambiamento controllare provenance, branch, stato Git, configurazione board/toolchain e differenze rispetto a `main`.

## M1 — ESP-NOW bidirezionale affidabile (IN CORSO, priorità assoluta)
**M1a DEV offline — sostanzialmente implementato, da mantenere.**
- Framing DRB2/CRC, S3 beacon RX, C6 beacon TX; simulazioni discovery, STATUS, PING/PONG, command diagnostico 0/ACK, heartbeat, ritrasmissioni e timeout.
- Coda TX, callback e timeout driver verificati su mock; build S3/C6 default e DEV riportate PASS nell'attuale PR #2.
- Restano possibili falsi positivi: test host e build NON costituiscono test ESP-NOW reale.

**M1b sicurezza e persistenza — prossimo blocco autonomo di coding.**
- Integrare senza regressioni `PairingRegistry`, `ReplayWindow`, `RadioAckTracker` con trasporto vero, con handshake esplicito autenticato e separazione completa DEV vs produzione.
- NVS per associazione ID↔MAC, stato canale e parametri indispensabili; piano provisioning LMK e ESP-NOW unicast cifrato. Evitare segreti nel repository e handshake fittizi.
- Gestione dei casi: peer errato, MAC/sessione/ID non corrispondenti, replay, ACK persi/tardivi, riavvio, scadenza peer, canale bloccato da AP, disconnessione.
- Per ogni modifica: unit test host e build S3/C6, default e DEV; controllo che DEV non sia attivo nei binari default.

**M1c collaudo fisico — BLOCCATO FINO A VIA LIBERA.**
- Seguire `FIRST_RADIO_TEST.md` verificando prima la procedura corrente; identificare schede/porte/antenna/partizioni e predisporre piano di rollback. Non eseguire flash senza consenso esplicito.
- Registrare prove reali S3↔C6, canale, RSSI, pacchetti bidirezionali, perdite, retry, tempi, recovery; poi pairing sicuro e comunicazione cifrata.
- Chiudere M1 solo con evidenza fisica e criteri sicurezza verificati. **PR #2 rimane draft e non va mergiata automaticamente.**

## M2 — Esperienza radar (successiva a M1)
- UI e launcher bottom-up con modalità Radar / Audiobookshelf / Impostazioni / Debug.
- Modalità Aperto GPS+ESP-NOW e Chiuso solo ESP-NOW; progressione prossimità con smoothing/isteresi RSSI e soglie per modalità.
- Non convertire RSSI in metri o direzione; ritrovamento sotto circa 30 cm deciso visivamente. Bussola serve orientamento interfaccia, non triangolazione RSSI. Fix GPS e log posizione sono opzionali in modalità Chiuso.
- Test host gesti, stati, rendering e persistenza; test hardware in sessione autorizzata.

## M3 — Sfera prototipo e consumi (1 unità prima di replicare)
- Verificare sul banco IMR14500 500 mAh + protezione PCM 1S, carica, connessioni, massimi di corrente e tensione; non assumere protezioni integrate.
- Grove vibrazione con driver e 3 WS2812B; nessun buzzer. Test logica attuatori **solo dopo** pairing e verifica elettrica. Interruttore; sleep e wake; autonomia misurata.
- Testare portata/applicazione RSSI outdoor/indoor e recovery; replicare soltanto dopo risultato affidabile.

## M4 — Funzioni opzionali e consolidamento
- Audiobookshelf sul radar come modalità distinta; configurazione sicura server NAS. Case definitivo, ergonomia, debug, regressioni, documentazione installazione/recupero.

## Checkpoint per Codex
Per ciascun commit significativo: aggiornare `STATUS.md` con versione/branch, evidenza test, limitazioni e prima azione successiva. Mai cancellare le verifiche già riportate; distinguere evidenze storiche, attuali e da ripetere.
