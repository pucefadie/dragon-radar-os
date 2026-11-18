# Dragon Radar OS — istruzioni operative per agenti Codex

Ultimo aggiornamento di coordinamento: 2026-11-18.
Repository: `pucefadie/dragon-radar-os`. Branch operativo attuale: `feat/espnow-m1-bidirectional`, PR #2 **draft** verso `main`.

## Missione e fonti
Realizzare Dragon Radar OS per Waveshare ESP32-S3-Touch-AMOLED-1.75-G e sfere fisiche XIAO ESP32-C6. Conservare l'implementazione diagnostica funzionante. All'avvio leggere nell'ordine: questo file, `STATUS.md`, `ROADMAP.md`, `README.md`, `RESUME.md`, `PROTOCOL_AND_POWER.md`, `FIRST_RADIO_TEST.md` (sul branch corrente), poi sorgenti e test. In caso di contrasto, fare riferimento all'evidenza tecnica più recente e segnare la divergenza in STATUS.md, senza inventare stati hardware.

## Regole non negoziabili
1. NON flashare ESP32-S3 o C6, NON cancellare flash/NVS, NON cambiare partizioni, NON attivare OTA, NON scrivere fuse, NON avviare prove fisiche né attuatori senza autorizzazione esplicita per quel test.
2. NON fare merge della PR #2, push su `main`, force-push, reset distruttivi, rebase distruttivi, cancellazione di tag/checkpoint o modifiche ai backup locali.
3. Prima di agire: `pwd`, `git status --short`, `git branch --show-current`, `git remote -v`, `git log -5 --oneline`. Non dare per scontato che la copia Ubuntu coincida con GitHub. Se il working tree contiene lavoro non salvato, non sovrascriverlo.
4. È consentito lavorare autonomamente **offline**: codice, test host, mock, build, documentazione e commit sul branch di lavoro; push normale dello stesso branch solo se è tracciato correttamente e non richiede forzature. Non lanciare script che flashano o contattano dispositivi.
5. Il percorso `RADIO_DEV_INSECURE=1` è esclusivamente diagnostico, in chiaro e non autenticato, solo sfera ID 1, comandi innocui, nessun motore/LED. Di default rimane DISABILITATO. CRC e MAC non sono autenticazione.
6. Le trasmissioni applicative che governano attuatori richiedono pairing deliberato, ID↔MAC persistente, LMK/cifratura unicast e difese da replay. Non dichiarare `Paired` senza handshake autenticato; non lasciare passare risposte DEV come autenticazione.
7. L'antenna U.FL del C6 deve essere presente prima di usare il percorso antenna esterna. Non alimentare il prototipo con la cella IMR14500 priva di protezione/validazione. Non pilotare motori o strisce LED direttamente dai GPIO in assenza di verifica elettrica.
8. Wi-Fi AP e ESP-NOW devono condividere il canale; non modificare arbitrariamente il canale del radar connesso. RSSI indica prossimità grossolana, NON distanza assoluta in metri e NON bearing. Non progettare una precisione centimetrica, UWB o sensori addizionali per determinare i 30 cm.
9. Non pubblicare credenziali, token, LMK, MAC reali, seriali personali, dump flash o log potenzialmente sensibili. Non copiare backup privati dal repository locale allo snapshot GitHub.

## Prodotto concordato
- Radar modalità **Aperto**: GPS + ESP-NOW; modalità **Chiuso**: ESP-NOW senza necessità di fix GPS. Le soglie RSSI/feedback vanno calibrate empiricamente e tenute distinte. Negli ultimi ~30 cm la sfera è visibile: il ritrovamento è una constatazione dell'utente, non una distanza stimata con precisione.
- Espandere da un'unica sfera-prototipo a ID 1–7 solo dopo verifica sicura; GPS registra la posizione quando disponibile, non localizza direttamente la sfera tramite il solo RSSI.
- Sfera: Seeed XIAO ESP32-C6, IMR14500 3,7 V 500 mAh, motorino Grove con driver integrato, tre WS2812B e interruttore; **nessun buzzer**. PCM 1S, circuito di ricarica, livelli logici, budget corrente, sleep e autonomia sono da verificare.
- Radar: AMOLED, touch, GPS, bussola BMM150, IMU, feedback vibrazione; alcune periferiche risultano collaudate in un progetto diagnostico precedente, non necessariamente nell'OS attuale. Menu dal basso con Radar, Audiobookshelf, Impostazioni, Debug è un requisito successivo alla radio affidabile.

## Ciclo di lavoro autonomo
1. Individua il prossimo deliverable **software non distruttivo** in `ROADMAP.md`. Specifica criteri di accettazione e file coinvolti.
2. Implementa in piccoli commit; esegui test host pertinenti con `-Wall -Wextra -Werror`, build S3/C6 sia default che DEV quando possibile, registra command line e risultato. Se una toolchain manca, scrivi BLOCKED/NOT RUN: non inventare PASS.
3. Risolvi autonomamente gli errori non rischiosi e ripeti test/build. Non fermarti per microdecisioni tecniche. Non ampliare arbitrariamente la milestone.
4. Aggiorna `STATUS.md` **in ogni checkpoint**: SHA/branch, PASS/FAIL/NOT RUN, artefatti NON sensibili, bloccanti, prossime tre azioni, richieste fisiche esatte. I commit possono essere numerosi, ma il report finale a utente deve essere conciso.
5. `git push` al branch tracciato se sicuro; verificane l'esito. Questo è il ponte verso ChatGPT. ChatGPT può leggere GitHub **quando interrogato**; non c'è dialogo automatico in tempo reale tra chat e Codex.
6. Fermati solo per flash/prove fisiche, cambio architetturale sostanziale, rischio dati o blocco reale. Se il contesto/token si esaurisce, salva `STATUS.md` e lascia una prossima azione precisa.

## Definition of Done
Per una milestone servono codice committato, test ripetibili, build richieste, registrazione delle prove mancanti e criteri verificati. Nessun test mock prova il funzionamento radio reale. La PR resta draft fino a un'autorizzazione distinta dell'utente.
