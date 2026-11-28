# Dragon Radar OS — handoff ChatGPT / Codex, 2026-11-28

## Provenienza e perimetro
- Base: `1ed0358` (riferimento origin/feat/espnow-m1-bidirectional disponibile localmente).
- Branch locale: `review/offline-20261128`.
- Worktree: `~/dragon-radar-offline-review-20261128`.
- Il worktree principale è rimasto intatto: era a 47bcc3e, 44 commit indietro, con sketch C6 modificato e diagnostics/ non tracciata. Non fare reset o checkout distruttivi.
- Nessun USB, seriale, OTA, flash, provisioning, modifica NVS, attuatore o backup. La v1 S3 già installata e la v332 C6 restano le baseline hardware storiche.

## Modifica funzionale
In src/proximity_engine.cpp, un ACK RSSI ritardato e più vecchio dell'ultimo campione valido poteva far interpretare la sottrazione unsigned received-lastSample_ come un intervallo enorme, azzerando il filtro e riportando indietro il timestamp. Un timestamp identico poteva inoltre pesare due volte nel filtro.
Il guard confronta entrambe le età rispetto allo stesso now, scartando timestamp vecchi o uguali mentre il filtro è valido. Conservati scadenza, reset dopo gap, rollover, invalidazione di sessione e comportamento al wake. Nessuna modifica a formato radio, autenticazione, retry, soglie, OTA, GPS o attuatori.
Non è dimostrato che questo difetto causasse i transitori Lost osservati fisicamente.

## Test
- RED: nuovo test host ha fallito prima della correzione (raw e filtered alterati).
- GREEN: 11 gruppi in scripts/review_offline_host.sh, exit 0 dopo la modifica e dopo l'estensione multisfera.
- ASan/UBSan, -Wall -Wextra -Werror: PASS.
- Casi nuovi: campione ritardato, duplicato, scadenza a 10 s, rollover e nuovo campione dopo gap; 21 cicli con sette filtri intercalati (147 campioni validi, 294 vecchi/duplicati), confronto con riferimenti ordinati.
- Ripetere: `bash scripts/review_offline_host.sh`.
- Build S3 UI Trial avviata con core 3.3.12 e ARDUINO_DIRECTORIES_DATA=$HOME/.local/share/dragon-arduino/data; stato da consultare nell'ultimo checkpoint STATUS.md.
- Build C6 e altri profili, radio vera, touch, GPS, OTA: NOT RUN.

## Ripresa Codex
1. Leggere questo file e STATUS.md nel worktree isolato; verificare git status e git log.
2. Integrare solo il commit di questo branch (cherry-pick su un worktree aggiornato e pulito); non sovrascrivere il worktree principale sporco.
3. Rieseguire suite host, completare build dei profili e collaudo M3f v1 già installata, dopo autorizzazione USB. La nuova build di revisione non è un aggiornamento autorizzato.
4. Indagare separatamente i drop RX/TX e i retry DRA1 correlando log temporali e contatori; non attribuirli alla correzione RSSI senza evidenza.


## Secondo intervento: launcher e navigazione
- Base del secondo intervento: 5b124fa. In src/app_manager.cpp, l'apertura del launcher via SwipeUp non aggiornava selected, a differenza di longPress; una successiva conferma fisica poteva aprire un'app diversa da quella corrente.
- Correzione: sincronizzare selected con current all'apertura via swipe; mantenute coordinate, gesture e comportamento del pulsante.
- RED: nuovo test app_manager_test.cpp fallisce sul codice precedente. GREEN: selezione corretta da Debug, riapertura, ciclo selezione con shortPress e conferma longPress.
- Suite host estesa con app-manager, gesture e standby-button: 14 gruppi PASS, exit 0. Nessuna prova fisica.
- La build S3 UI Trial avviata nella prima sessione usa lo staging precedente alla correzione launcher: anche se passa, NON valida il secondo intervento. Ricompilare da HEAD in una nuova directory.
- Integrare entrambi i commit del branch, in ordine, su una copia pulita. Nessun push effettuato.


## Terzo checkpoint: specifica grafica M2d e risultato build
- Specifica dettagliata in RADAR_UI_SPEC_20261128.md. L'utente ha scelto la specifica tecnica (opzione B), non l'implementazione in questo checkpoint.
- Build S3 UI Trial del primo intervento RSSI: PASS, exit 0, core 3.3.12. Sketch 1.765.863 byte (56%), RAM globale 72.948 byte (22%). BIN 1.766.016 byte, SHA-256 a86a1b260c755d2335e606a8ace482590b50e35310c7b5d3a60e06ed93b1d45a. Percorso locale build/review-20261128-ui/artifacts/dragon_radar_sketch.ino.bin. Non include launcher ea4e3e8; NON installare.
- Commit precedenti: 5b124fa RSSI; ea4e3e8 launcher. Suite host finale 14 gruppi PASS. La nuova specifica non cambia sorgenti né risultati di test.
- Ripresa: integrare i commit in ordine dal branch review/offline-20261128 su worktree pulito, leggere la specifica M2d, implementare helper puro + renderer Chiuso con test, compilare candidati distinti. Preservare Demo/Aperto e gate M3f hardware. Nessun merge automatico.


## Quarto checkpoint — feedback aptico direzionale implementato
- Base fb76ca6, branch review/offline-20261128. Implementazione S3 soltanto: in Reale Chiuso ogni attraversamento di fascia 10% del livello RSSI filtrato genera uno short 100 ms in salita o double 100/120/100 ms in discesa, non una sequenza per ciascuna fascia saltata. Nessun impulso iniziale e nessun arretrato; cooldown 1500 ms, rispetto di pattern_running e driver inizializzato.
- DirectionHapticGate in include/radar_feedback.h, nuova sequenza DOUBLE nel driver locale, epoch in beacon_service per selezione/sessione/nuova partita/sospensione, reset in main per app/launcher/modalita/vibrazione/OTA/standby. Non modifica Demo/Aperto, DRA1, firmware C6, soglie RSSI o feedback remoto al ritrovamento. Nessuna vibrazione aggiuntiva Lost/Found opzionale.
- PASS 15 gruppi host: bash scripts/review_offline_host.sh, exit 0. Test nuovo copre salita/discesa, multi-step, cooldown, busy, assenza di replay, reset, ID/sessione e rollover. Non equivale a test elettrico.
- Build S3 UI Trial candidata v3 avviata con core 3.3.12: ARDUINO_DIRECTORIES_DATA=$HOME/.local/share/dragon-arduino/data ARDUINO_CLI=$HOME/.local/dragon-arduino-cli/arduino-cli RADAR_UI_TRIAL=1 RADAR_OTA_VERSION=3 bash scripts/build_linux.sh build/review-20261128-haptic-v3. Esito da verificare. Non installare né usare come candidato OTA approvato. Grafica M2d resta SPEC.
- Prossimo: verificare build e binario, test fisico autorizzato delle due sequenze e tenuta alimentazione, implementare renderer M2d separatamente. Conservare il gate M3f e le baseline.


## Quinto checkpoint — grafica Chiuso implementata
- Build precedente S3 UI Trial v3 (commit 2eb44b0) PASS, exit 0. Sketch 1.766.403 byte (56%), RAM globale 72.964 byte (22%), BIN 1.766.544 byte SHA-256 bcb4187cf16a99bdef3ed98cd29f68c2bf58d61184796a76561959a239018de9. Nessun flash/OTA.
- Base sorgenti 2eb44b0. Implementazione grafica: drawClosedRadar in src/display_module.cpp, richiamato esclusivamente per !v.demo && !v.open. Reticolo verde ruotato e ritagliato, scanner dietro sfera centrale, 15 raggi da 12 a 68 px, stelle 1..7 per ID da r>=24, percentuale relativa RSSI, stati searching/lost/found e selettori/azioni con contrasto elevato. Eliminato triangolo rosso e anelli gialli solo in Chiuso; Demo/Aperto e hitbox invariati. Nessun nuovo buffer o asset.
- Helper puro include/radar_ui_geometry.h e test tests/radar_ui_geometry_test.cpp: estremi, monotonia, clamp, stati e geometria non sovrapposta delle stelle per tutti gli ID. Suite host 16 gruppi PASS, exit 0. Non sono test di rendering fisico o frame rate.
- Build S3 UI Trial v4 avviata: ARDUINO_DIRECTORIES_DATA=$HOME/.local/share/dragon-arduino/data ARDUINO_CLI=$HOME/.local/dragon-arduino-cli/arduino-cli RADAR_UI_TRIAL=1 RADAR_OTA_VERSION=4 bash scripts/build_linux.sh build/review-20261128-graphics-v4. Esito da verificare. Non installare né usare come OTA approvato. V4 comprende anche le correzioni precedenti e haptic.
- Prossimo: verificare exit code e artefatti v4; test fisico autorizzato M3f v1 prima di nuovo candidato, misurare fluidità, contrasto e leggibilità; verificare touch e feedback aptico. Altri profili e hardware NOT RUN. Nessun merge automatico.


## Sesto checkpoint — leggibilità launcher
- Implementato size 2 nelle tile, etichette su due righe, contorno selezionato, istruzioni leggibili; coordinate e hit test condivisi via launcherTileAt/X/Y e costanti in app_manager.h. Tap nelle separazioni non apre app, pulsanti e gesture preservati.
- Suite host 16 gruppi PASS, exit 0. La build v4 in corso è del renderer Chiuso prima di questo checkpoint, non contiene il nuovo launcher. Per candidato cumulativo usare RADAR_OTA_VERSION=5 e output nuovo (build/review-20261128-graphics-v5), non installare senza autorizzazione. Debug/Aperto non ridisegnati; banco NOT RUN.
