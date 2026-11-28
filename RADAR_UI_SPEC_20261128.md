# M2d — specifica grafica Dragon Radar (ChatGPT, 2026-11-28)

Stato: proposta tecnica pronta per implementazione, NON implementata né collaudata.
Richiesta: migliorare leggibilità e fedeltà all'estetica del radar di Dragon Ball; in Reale Chiuso sostituire il triangolo rosso centrale con la sfera selezionata, che cresce all'avvicinamento.
Base: 1ed0358 + 5b124fa (RSSI) + ea4e3e8 (launcher). Vedere CODEX_HANDOFF_20261128.md.

## 1. Direzione visiva
AMOLED rotondo 466x466, centro (233,233), raggio utile circa 222. Riprendere il quadrante verde luminoso, reticolo ortogonale inclinato di 60 gradi già presente, cornice e indicatori giallo/ambra tipici del radar manga/anime. Niente pannelli futuristici decorativi, gradienti pesanti o asset esterni: primitive GFX, stelle disegnate proceduralmente.
- Fondo verde medio #24A84A, reticolo verde scuro #14552B, testi scuri #092915 su fondo chiaro. Pannelli scuri per azioni con testo bianco #FFFFFF; ambra #FFB020, rosso #D72D25 per stelle e avvisi. Convertire RGB888 in RGB565 con la utility esistente o constexpr; verificare resa AMOLED.
- Griglia ritagliata al quadrante circolare (r=190), passo 45 px e spessore 1-2 px; non sovrapporre griglia e scanner ai testi.
- Conservare la scansione a 6 s, discreta e dietro la sfera; in Chiuso sostituire i tre anelli gialli di livello con la sfera scalata, evitando doppia codifica e sovrapposizioni.
- Gerarchia: stato principale e ID/testo azioni a size 2 o 3 (font standard GFX: circa 12x16 o 18x24 px per carattere); dettagli a size 1 solo in Debug. Misurare larghezza, centrare e usare etichette brevi. Non usare solo colore per distinguere stati.
- In Chiuso togliere heading, CAL/RAW e avvisi bussola dalla pagina principale: non esiste bearing RSSI. Restano in Debug; Aperto mantiene bussola, orientamento, GPS e indicatori di distanza geografica.

## 2. Layout Chiuso — coordinate proposte
Tutte le coordinate y del testo sono top-left per il font GFX. Verificare clip sul cerchio fisico; usare margine >= 6 px. Disegno nell'ordine: fondo -> reticolo/scanner -> sfera -> pannelli e testi.
| Elemento | Geometria / contenuto |
|---|---|
| Titolo | y=62, size 2: SFERA 1 · 0/7 (usare caratteri ASCII nella build) |
| Modalità | y=91, size 2: CHIUSO |
| Selettore ID 1–7 | mantenere x=81+44*(id-1), y=122, w40 h27 e hitbox esistenti; cifre size 2, contorno marcato per selezionata, simbolo per trovata |
| Sfera centrale | (233,233), r=12..68, occupazione massima y=165..301, lasciando margine rispetto al selettore e allo stato |
| Stato principale | y=308, size 2; testo breve: DEBOLE / STABILE / TI AVVICINI / TI ALLONTANI / VICINO / SEGNALE PERSO / TROVATA |
| Stato radio | y=334, size 2, es. CONNESSO / RICERCA; evitare duplicazione se già espresso sopra |
| Conferma | hitbox invariata x92 y356 w282 h36; testo size 2: TROVATA? CONFERMA oppure CONFERMA RITROVAMENTO se entra |
| Nuova partita | hitbox invariata x146 y397 w174 h26; testo size 2; non mostrare se foundCount=0 |
| Launcher | indicatore esistente x207 y432 w52 h5 e swipe dal basso invariati |
Usare uno sfondo opaco dietro testo se il reticolo lo attraversa. Verificare che testo size 2 non sfori; ad esempio CONFERMA RITROVAMENTO ha 21 caratteri, 252 px nominali. Il centro della sfera è fisso: NON rappresenta una direzione.

## 3. Sfera centrale, scala e stati
In Reale Chiuso, usare esclusivamente RadarView.proximity.level (1..15), già filtrato e dotato di isteresi; render solo per peer selezionato, Paired, fresh e non Found. Nessun uso di RSSI grezzo per dimensione. Non stimare metri.
Raggio intero: r = 12 + ((level-1)*56 + 7)/14; clamp level a 1..15; per level=0, nessun segnale valido. Diametri: livello 1=24, 5=56, 8=80, 12=112, 15=136 px. L'area cresce non linearmente; il raggio è lineare rispetto al livello, non alla distanza fisica.
- Sfera ambra/arancione, contorno scuro 2 px, highlight chiaro in alto a sinistra; stelle rosso scuro a cinque punte, numero corrispondente all'ID (1–7). Per r<24 mostrare almeno il disco e il numero ID leggibile nel titolo, senza micro-stelle; quando r>=24 usare layout normalizzato e 1–7 stelle senza sovrapposizioni, tutte entro r-4.
- Disponibile: raggio aggiornato direttamente sul livello filtrato; eventuale animazione di interpolazione solo visuale (max 200 ms, senza delay), non alterare stato, conferma o soglie. Prima implementazione senza pulsazione per misurare fluidità.
- Searching / peer non Paired: contorno neutro tratteggiato r=12, testo RICERCA; niente sfera piena.
- Lost / stale: rimuovere immediatamente sfera piena e mostrare contorno neutro, SEGNALE PERSO; mai mantenere ultima dimensione.
- Nessun ID associato/selezionato: nessuna sfera, NESSUNA SFERA.
- Found: sfera fissa r=40 con spunta e TROVATA; non abilitare di nuovo conferma. Progressione gioco invariata.
- Cambio ID, standby, nuova partita e cambio sessione: nessuna interpolazione o stato visuale ereditato da un altro peer.
- Conferma resta volontaria tramite pulsante esistente, non automatica alla massima dimensione.

## 4. Distanza simulata e altre modalità
DEMO attuale (app_radar.cpp DemoProvider, RadarView.nearest_m e balls) conserva mappa, triangolo e blip. Non equiparare !v.open a Reale Chiuso: usare !v.demo && !v.open.
Per una preview offline opzionale di Chiuso usare un harness host separato con livelli 0..15; se serve una distanza simulata, definire un cursore dNorm in [0,1] (0 vicino, 1 lontano) con level=1+round(14*(1-dNorm)); simulazione soltanto, nessuna radio/GPS/attuatori. Non modificare la modalità Demo esistente per introdurre questa preview senza decisione esplicita.
APERTO mantiene triangolo rosso, blip geografici, zoom, salvataggio GPS e fallback RSSI. Modifiche grafiche generali solo dopo regressioni, separatamente dalla nuova UI Chiuso.

## 5. Implementazione suggerita
- src/display_module.cpp: displayRenderRadarApp(const RadarView&), attualmente circa riga 1474. Separare drawClosedRadarBody() dal percorso Demo/Aperto. Il triangolo è disegnato intorno a riga 1498 e gli anelli intorno a 1554; in Chiuso non eseguire quei percorsi.
- include/app_radar.h: costanti hitbox RADAR_SPHERE_*, RADAR_FOUND_*, RADAR_GAME_*. Conservarle; src/main.cpp usa le stesse coordinate nella dispatch gesture.
- Aggiungere include/radar_ui_geometry.h (helper puro, senza Arduino): closedSphereRadius(uint8_t level), classificazione visuale con input selected/paired/fresh/found e geometria stelle. Nessuna allocazione dinamica.
- src/app_radar.cpp già restituisce v.proximity in Chiuso, senza GPS: non serve cambiare modello dati, DRA1 o ProximityEngine.
- Rispettare getDrawTarget()/flushDisplay(), canvas PSRAM esistente e cadenza render. Nessun nuovo framebuffer full-screen, sleep, delay, I/O o rendering nel callback radio.
- Launcher, impostazioni e Debug: seconda fase; titoli su due righe nelle tile, etichette brevi, size 2 per azioni. Aggiornare sempre disegno e hitbox insieme; non alterare procedure OTA.

## 6. Criteri di accettazione e test
1. Host: livello 0 (assenza), 1..15 monotoni, estremi 12/68, input >15 clamp, sfera non oltre y=301. Stato stale, Lost, Found, non Paired, ID=0 e switch ID.
2. Host: 7 ID distinti e stelle entro il disco; layout e rettangoli disgiunti; prova ripetibile dei livelli simulati 0,1,5,8,12,15 con preview PNG/SVG facoltativa, non asset firmware.
3. Regressione: bash scripts/review_offline_host.sh, aggiungendo nuovo test radar_ui_geometry. Eseguire build UI Trial S3 e standard, DEV se pertinente; versionare candidato distinto da v1 installata, niente OTA.
4. Banco successivo autorizzato: prima verificare M3f v1 già installata, poi eventualmente installare nuova candidata separata. Controllare leggibilità reale, touch ID/Conferma/Nuova partita, transizioni RSSI, assenza di flicker, timing frame, gioco 0/7..7/7 e regressione Demo/Aperto. Non dichiarare PASS fisico da test host.
5. Mantenere OTA0 originale protetta e nessuna modifica NVS, firmware C6, chiavi o pin.

## 7. Decisioni da confermare sul display
Colori, raggio massimo 68, soglia per stelle, testo breve e assenza di pulsazione sono scelte grafiche proposte, non già approvate o validate fisicamente. La fedeltà è al linguaggio visivo del quadrante, non a una specifica tavola o fotogramma. Non sono stati prodotti mockup né modificato il renderer in questo checkpoint.


## 8. Feedback aptico direzionale in Reale Chiuso — richiesta utente
Stato: IMPLEMENTATO per i due pattern direzionali, test host PASS; build S3 v3 da verificare e collaudo fisico NOT RUN. Vibra il radar S3 in mano all'utente, non il motore della sfera remota. Il feedback remoto al ritrovamento rimane invariato.

- Usare lo stesso livello RSSI filtrato e con isteresi della grafica (1..15). Percentuale relativa p=100*level/15; fascia intera b=(10*level)/15 con divisione intera, 0..10. Il livello 0 significa assenza, non distanza massima. La percentuale non è una misura in metri.
- Attraversamento di una o più soglie del 10% verso l'alto: un impulso di 100 ms. Verso il basso: due impulsi di 100 ms separati da 120 ms OFF. Segnale stabile: silenzio. Un salto multiplo produce un solo pattern; nessuna coda di impulsi.
- Al primo campione valido, ritorno da Lost, cambio ID/sessione, nuova partita, ingresso in Chiuso o riattivazione vibrazione, acquisire la fascia di riferimento senza vibrare. Non confrontare misure appartenenti a peer diversi. Solo nuovi campioni autentici, non i frame UI ripetuti (timestamp ricezione ricavabile da now-age_ms con snapshot coerente).
- Requisiti: selected 1..7, Paired, fresh, non Found, impostazione vibration attiva, Radar Chiuso in primo piano e nessun OTA/standby. Invalidare il riferimento quando uno dei gate decade; anche quando il launcher copre il Radar o l'app cambia. Nessun fallback ai beacon DEV o a RSSI grezzo.
- Anti-rimbalzo: sfruttare l'isteresi RSSI esistente e introdurre intervallo minimo tra nuovi pattern di 1500 ms (parametro proposto da calibrare); se il motore ha un pattern in corso, non interromperlo, nemmeno durante la pausa OFF. Aggiornare comunque la fascia osservata e scartare gli eventi soppressi, evitando vibrazioni ritardate. Nessun delay bloccante.
- Opzionale, da approvare separatamente: tre impulsi una sola volta nella transizione da segnale valido a Lost; un impulso più lungo (250 ms) alla conferma manuale di ritrovamento, senza interferire con il feedback remoto esistente. Non necessari per la prima implementazione dei due segnali direzionali.

### Integrazione e collaudo
- appRadarHaptic() in src/app_radar.cpp oggi esce per !v.demo: mantenere Demo invariata e aggiungere percorso Chiuso distinto. src/main.cpp chiama il feedback nel render (~60 ms), solo in app Radar e senza OTA; aggiungere reset esplicito quando non viene chiamato e sui cambi di modalità/peer/sessione. Aperto invariato.
- Estendere include/radar_feedback.h con macchina a stati pura host-testable; input validità, livello, identità/sessione, timestamp e abilitazione; output None/Approach/Retreat. Non modificare la logica di ricerca o di conferma.
- Driver locale: esistono motorTriggerShortPulse() (100 ms), motorTriggerMediumPulse() (250 ms) e motorTriggerTriplePulse() (3x100/120 ms); non esiste ancora un doppio pattern dedicato. Aggiungere DOUBLE al motore locale con tre step ON/OFF/ON in peripherals_module.cpp e relativo enum/helper. Conservare limiti hardware, watchdog, diagnostica e gestione non bloccante. Verificare pattern_running, non soltanto motorIsActive(), per non interrompere le pause OFF. Nessun comando DRA1 alle sfere per questo feedback.
- Test host: soglie 10%, salti multipli, ripetizioni, oscillazioni, cooldown e rollover, Lost e reacquisizione, ID/sessione diversa, standby/OTA/launcher, vibrazione disattivata, Found. Verificare che un motore occupato non generi arretrati. Aggiornare suite host e build S3. Banco solo dopo autorizzazione: percepibilità dei pattern e misure elettriche; nessuna variazione al firmware C6.
