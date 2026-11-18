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

**Commit firmware verificato:** `7ff019d`.

**Attività appena conclusa:** DRA1 standard per una sfera (ID 1), peer ESP-NOW cifrato, handshake HMAC a doppia sfida, anti-replay, ACK diagnostico e retry, schema NVS versionato, generatore offline e utility di provisioning cablata. DEV separato, coda driver a un solo invio in volo, aggiunta peer limitata e recupero da callback tardiva. Vettori binari DRB2 broadcast/directed.

**Test PASS:** 8 suite host con `g++ -std=c++11 -Wall -Wextra -Werror` e `python3 tests/make_radio_records_test.py` (comandi nel README). Coperti handshake HMAC, ACK perso, retry, MAC o frame errati, replay dopo riavvio, scadenza, vettori DRB2, overflow e timeout TX, schema/permessi e rifiuto sovrascrittura dei record. **Test FAIL:** nessuno a fine verifica.

**Test NOT RUN:** collaudo fisico bidirezionale e provisioning su scheda. Come evidenza storica distinta, il ricevitore minimale separato ha ricevuto 10 DRB2 validi confermati sul canale 6 e l'intera NVS era identica dopo il test. Quella prova non verifica DRA1.

**Build PASS:** Arduino ESP32 3.3.12. S3 con `RADIO_DEV_INSECURE=0|1 scripts/build_linux.sh <nuova-directory>`. C6 con `RADIO_DEV_INSECURE=0|1 DRAGON_BEACON_OUTPUT=<nuova-directory> scripts/build_beacon.sh 1 6 esp32:esp32:XIAO_ESP32C6`. Utility di provisioning separata compilata con entrambi gli FQBN. Snapshot locale ignorato da Git `build/m1-review-VpK1MB`, con log e manifest. I binari standard contengono il marker sicuro e non quello DEV. Validati target e integrità immagine su tutti e sei i binari. I sorgenti radio nello staging corrispondono a `7ff019d`; le modifiche locali dell'antenna C6 non sono incluse nei binari verificati.

| Immagine applicativa | Byte del binario | SHA-256 |
|---|---:|---|
| S3 standard | 1782000 | `d8e31f154e1e51c4d0636d37b2b949c29b96d4ea4f0631e60351461bfd057a9f` |
| S3 DEV | 1779120 | `7258e83939fdd7ab8c544cf434760023b3952efcb987c13114f4f24bc10c507d` |
| C6 standard | 970832 | `99b1a70dc55aa68b12a7c6b8c50b8328a38a25d5febb04a061a0308f7d333a39` |
| C6 DEV | 967312 | `f6bb6a9b6fc197976ca16eb8bd167fe42a8d3ef6889c97c89385af113cf1b270` |
| Provisioning S3 | 312112 | `7601bc33f7613721085b4135f344319289d5e8b6a5976e1b8e873cceca6f3eb5` |
| Provisioning C6 | 262976 | `0fef57e8624b3d315ea63f63f464b06e73830b375a3be24bbaecdd5036c041af` |

**Hardware toccato in questo ciclo:** NO. **Pubblicazione:** checkpoint destinato al solo branch tracciato, in fast-forward. PR #2 draft.

**Blocchi reali:** manca l'evidenza fisica per M1c. Cifratura a riposo, rotazione/revoca, ricerca automatica del canale e supporto multiplo sono fuori dal prototipo a singolo peer. I record NVS sono leggibili da chi accede alla flash senza cifratura; non usare fuori banco prima della scelta sulla protezione a riposo. Il recupero da cambio canale presuppone il riallineamento; in caso di callback TX definitivamente persa, riavviare.

**Prossime tre azioni:** rivedere protezione a riposo e procedura di provisioning/rollback; in sessione autorizzata provisionare una coppia su USB e provare il collegamento cifrato; misurare perdita/ACK e recupero da riavvio o cambio canale con prove negative e log correlati. M1 resta aperta. L'intervento utente è necessario solo per la decisione sui segreti a riposo e per autorizzare e predisporre il prossimo banco.
