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
