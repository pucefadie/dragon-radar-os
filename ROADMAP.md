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

**M1b sicurezza e persistenza — implementata, verifiche software in mantenimento.**
- Integrare senza regressioni `PairingRegistry`, `ReplayWindow`, `RadioAckTracker` con trasporto vero, con handshake esplicito autenticato e separazione completa DEV vs produzione.
- NVS per associazione ID↔MAC, stato canale e parametri indispensabili; piano provisioning LMK e ESP-NOW unicast cifrato. Evitare segreti nel repository e handshake fittizi.
- Gestione dei casi: peer errato, MAC/sessione/ID non corrispondenti, replay, ACK persi/tardivi, riavvio, scadenza peer, canale bloccato da AP, disconnessione.
- Per ogni modifica: unit test host e build S3/C6, default e DEV; controllo che DEV non sia attivo nei binari default.

**M1c preflight e collaudo fisico — PASS sul profilo di banco.**
- Seguire `FIRST_RADIO_TEST.md` verificando prima la procedura corrente; identificare schede/porte/antenna/partizioni e predisporre piano di rollback. Non eseguire flash senza consenso esplicito.
- Registrare sul banco S3↔C6 l'handshake autenticato, gli ACK, il canale effettivo, RSSI, perdite, retry e recupero. Usare i contatori DRA1 e una fixture approvata per i casi negativi mirati.
- Chiudere M1 solo con evidenza fisica e criteri sicurezza verificati. **PR #2 rimane draft e non va mergiata automaticamente.**

**M1d configurazione PMK standard — completata sul banco con DRC1.**

- DRC2 con PMK casuale per dominio e LMK/HMAC per coppia. Lettura DRC1 compatibile con il banco senza scrivere NVS. Priorità DRC2 senza downgrade su errori.
- Il percorso radio standard con DRC1 esistente ha superato handshake, conferme applicative e recupero dopo il riavvio di ciascuna scheda. Il collaudo DRC2, la cattura CCMP indipendente e il ripristino effettivo da ROM restano da verificare.
- Il banco M1d era limitato a ID 1. M3 multisfera mantiene una PMK per radio e una configurazione per peer.

## M2 — Esperienza radar (successiva a M1)
- **M2a completata** stato DRA1 della sfera ID 1 nel Radar e in Debug, banco S3 UI Trial offline e C6 sul canale 6. Verificati pairing, UI e ripresa dopo standby.
- **M2b Chiuso** RSSI degli ACK DRA1 autenticati, filtro con isteresi, conferma manuale del ritrovamento e diagnostica della freschezza. Test software e collaudo fisico qualitativo passati.
- **M2b.1 scala RSSI** 15 livelli grafici su tre anelli, con più risoluzione a segnale forte. Isteresi grafica separata da qualità RSSI e stato DRA1. Test host e build S3 UI Trial passati. Confronto sul campo con M2b ancora da eseguire con la configurazione radio già collaudata.
- UI e launcher bottom-up con modalità Radar / Audiobookshelf / Impostazioni / Debug.
- **M2c Aperto** GPS del radar, bussola e riferimenti geografici facoltativi per ID, con RSSI disponibile anche senza fix. Test host e build S3 UI Trial passati. Candidato e identità USB verificati. Preflight live da completare dopo conferma dell’isolamento degli attuatori. Prova geografica reale rinviata.
- Non convertire RSSI in metri o direzione; ritrovamento sotto circa 30 cm deciso visivamente. Bussola serve orientamento interfaccia, non triangolazione RSSI. Fix GPS e log posizione sono opzionali in modalità Chiuso.
- Test host gesti, stati, rendering e persistenza; test hardware in sessione autorizzata.

## M3 — Sfera prototipo e consumi (1 unità prima di replicare)
- Verificare sul banco IMR14500 500 mAh + protezione PCM 1S, carica, connessioni, massimi di corrente e tensione; non assumere protezioni integrate.
- **M3a PASS funzionale via USB** impulso di avvio, comando seriale, cooldown e stabilità DRA1 sotto carico verificati. Durate elettriche, transitori di reset e tenuta della 3V3 restano da misurare. Il GPIO D5 è stato riservato ai LED RGB, poi installati in coppia in M3d.
- **M3b PASS funzionale via USB** comando manuale DRA1 da Debug. Test host, build S3/C6, installazione, 60 s DRA1 e prova supervisionata del Grove passati. Due impulsi ai tocchi accettati e nessun impulso durante il cooldown. Restano aperte le misure strumentali M3a.
- **M3c PASS funzionale** l'operatore ha confermato il collaudo del feedback al ritrovamento. Le app S3 M3c e C6 M3b costituiscono il checkpoint storico del collaudo.
- **M3 multisfera** sette peer autenticati, selezione, vibrazione indirizzata, ritrovamenti e Nuova partita. Simulazione con sette sfere e build S3/C6 completate. PASS funzionale con la sola sfera ID 1 confermato dall’operatore. Gli artefatti S3 multisfera e C6 M3b sono la baseline attuale. Provisioning e collaudo con più sfere restano aperti.
- **M3d RGB** due WS2812B su D5, effetti di stato e diagnostica per ID via DRA1. Rosso a bassa luminosità confermato con l’app temporanea. Build S3 e C6 passate. C6 installato con readback e conferma visiva di bianco e attesa rossa. Restano le misure elettriche e la prova con radar e Grove.
- **M3e OTA C6** manutenzione esplicita via USB o DRA1, trasferimento diretto PC → sfera, firma per ID, due slot e bootloader con rollback. Migrazione USB ID 1 e readback passati. Primo avvio v301 con reinizializzazione anomala di un settore NVS. Recupero M3e.1 verificato. La v311 ha bloccato la cancellazione durante il mount e preservato la NVS. M3e.2 conferma il mount del dump originale con driver ESP-IDF su host. Traccia v321 conferma letture raw errate, mappatura corretta e mismatch loader DIO/app QIO. Loader rollback QIO confermato sul C6 ID 1 con letture raw, direct e mapped corrette e NVS intatta. PASS sul C6 ID 1 per v331 USB, timeout AP e primo OTA firmato v331 → v332 VALID, con NVS intatta. Radar cumulativo installato e Paired con ID 1. Interazioni touch, RGB/Grove, attivazione OTA dal radar e rollback v333 ancora da collaudare. v312/v313 sospese. [Procedura OTA](SPHERE_OTA.md)
- **Indicatore di ricarica** rendere visibile il LED di ricarica originale della XIAO con una finestra nel tappo della sfera. La scelta esclude la segnalazione di ricarica tramite WS2812B e richiede solo una modifica meccanica al tappo. Palette, animazioni e GPIO restano quelli di M3d.
- Interruttore, sleep, wake e misure di autonomia restano sviluppi successivi.
- Testare portata/applicazione RSSI outdoor/indoor e recovery; replicare soltanto dopo risultato affidabile.

## M4 — Funzioni opzionali e consolidamento
- Audiobookshelf sul radar come modalità distinta; configurazione sicura server NAS. Case definitivo, ergonomia, debug, regressioni, documentazione installazione/recupero.

## Checkpoint per Codex
Per ciascun commit significativo: aggiornare `STATUS.md` con versione/branch, evidenza test, limitazioni e prima azione successiva. Mai cancellare le verifiche già riportate; distinguere evidenze storiche, attuali e da ripetere.
