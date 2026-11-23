# Dragon Radar OS — requisiti del gioco e triage dell'audit indipendente

**Decisione dell'utente: 23 novembre 2026.**  
**Baseline esaminata:** `1f14661f215e9e2b5919a990737ed2755c918ad9` (M1d).  
**Ambito:** decisioni di prodotto e priorità di manutenzione. Questo documento **non** dichiara corretti bug, non autorizza flash o provisioning e non sostituisce `STATUS.md`.

## 1. Scenario d'uso e compromessi deliberati

Dragon Radar OS è un **gioco elettronico domestico per bambini**, costruito e gestito dal proprietario, non un prodotto commerciale o un sistema con dati particolarmente sensibili. L'ambiente previsto è fidato: non è richiesto un modello di minaccia orientato ad hacker esterni o ad attacchi informatici sofisticati.

- Priorità: funzionamento offline e all'aperto, recupero automatico del collegamento, semplicità, batteria/autonomia, affidabilità di firmware e associazioni, diagnosi e riparazione agevoli.
- **La password OTA di soli 3 caratteri è esplicitamente accettata dall'utente.** Non aumentarla né introdurre TLS, PKI, Secure Boot, Flash Encryption, eFuse o firme di firmware soltanto in risposta al suo basso livello di entropia. L'accettazione riguarda il rischio di accessi alla rete, **non** il rischio di perdita del firmware originale.
- Conservare l'attuale DRA1/ESP-NOW cifrato con autenticazione, salvo correzioni funzionali indispensabili. Non riscrivere il protocollo e non aggiungere hardening contro minacce non pertinenti.
- Mantenere disponibili i comandi diagnostici locali via USB per il proprietario. Valutare soltanto azionamenti accidentali, interferenze col gioco e protezione di operazioni distruttive.
- **La sicurezza fisica dei bambini non è un compromesso:** celle al litio protette, ricarica corretta, isolamento e fissaggio dei componenti, evitare corti/surriscaldamento e misurare il comportamento degli attuatori prima di richiuderli nelle sfere. Batterie e accessori sono temporaneamente scollegati dal banco M1c/M1d.

Queste decisioni possono essere riviste se cambia il contesto (uso pubblico, rete non fidata, distribuzione commerciale, dati personali, ecc.).

## 2. Non bloccare Codex per l'audit intero

L'audit indipendente Gemini sul commit M1d ha segnalato F-01–F-12, R-01–R-03 e U-01–U-02. Trattarli come **segnalazioni da verificare**, non come bug automaticamente provati o come obbligo di refactoring globale.

La milestone M1c ha già dimostrato sul banco un link DRA1 bidirezionale in configurazione con PMK comune. Il codice M1d DRC1/DRC2 ha superato verifiche offline ma **non** il collaudo M1d fisico. Non bloccare le normali attività di sviluppo e test offline per correggere immediatamente tutto.

### Gate A — Prima di una nuova prova M1d di banco con DRC1 esistente

Il test mirato del percorso radio isolato, **senza nuovo provisioning DRC2**, non richiede di risolvere prima tutte le criticità del firmware S3 completo. Restano necessari: revisione concreta dei binari, backup live attuali, verifica partizioni, recupero ROM, distinzione delle operazioni autorizzate e mantenimento della configurazione senza batteria/attuatori. Nessun flash è implicitamente autorizzato da questo documento.

### Gate B — Prima di scrivere un nuovo record DRC2 nelle NVS

**F-02 (alta):** rendere affidabile il trasferimento USB e l'installazione DRC2 con controllo di integrità, gestione del troncamento, conferma tramite rilettura e procedura di recupero. Preferire una soluzione semplice (CRC/hash del record e confronto dopo scrittura), senza introdurre autenticazione USB complessa. Conservare DRC1 e non cancellare automaticamente NVS; una transizione DRC1→DRC2 va coordinata sui due dispositivi.

### Gate C — Prima di utilizzare il firmware standard integrato S3/C6 e l'OTA

1. **F-01, parte operativa (alta):** impedire che un aggiornamento di prova sovrascriva OTA0 originale quando il radar si avvia da OTA1; evitare aggiornamenti simultanei incoerenti. **Non modificare** la password OTA di 3 caratteri per ragioni di hardening. Se il vincolo OTA0 non è garantibile, tenere OTA disabilitato nella build di prova.
2. **F-04 (alta):** rendere operativo ESP-NOW senza router; coordinare canale DRA1 e Wi-Fi nelle modalità Aperto/Chiuso, quando connesso, disconnesso o con AP su canale diverso. Non lasciare il gioco perennemente in `aligned=0`.
3. **F-06 (alta):** evitare cancellazione automatica/silenziosa della NVS con pairing e impostazioni; vincolare il C6 al layout fisico `no_ota` e sottoporre gli artefatti a un controllo di partizioni/dimensioni prima del flash.
4. **F-11 (prima di ricollegare il motore S3):** preparare GPIO16 con latch LOW prima di abilitare l'uscita, usando un'impostazione prudente; il firmware non può garantire da solo il livello durante ROM/boot/PMIC: verificare elettricamente con hardware reale.
5. **F-05 (bassa, non blocco generico):** mantenere la diagnostica USB locale; ridurre solo il rischio di invocazione accidentale di azioni distruttive/attuatori quando necessario.

### Gate D — Prima dell'uso autonomo delle sfere a batteria

- **F-03 (media/alta per affidabilità):** recupero da mancata callback TX o cambio modalità Wi-Fi, senza riattribuire erroneamente una callback tardiva. Non basta liberare `inflight_` a timeout.
- **F-09 (media):** evitare log USB periodici che rallentino il ciclo o consumino energia senza host, migliorare la diagnostica di avvio.
- Batterie e ricarica: protezione da sovracorrente/sovrascarica/sovraccarica, caricatore 1S appropriato, assemblaggio e involucro sicuri; verificare autonomia e temperature. La protezione elettronica non va rinviata a dopo l'uso da parte dei bambini.

### Miglioramenti rinviabili e non bloccanti oggi

- **F-07:** replay/DoS su Hello: verificare il rischio effettivo e i test; non fare hardening anti-attacco prioritario.
- **F-08 / R-03:** copia LMK in RAM e segreti NVS/flash in chiaro: accettati per prototipo fidato; pulizia semplice del codice possibile, senza migrazione crittografica invasiva.
- **F-10:** estendere i test host di `SecureEspNow` e provisioning dove aiutano a prevenire regressioni, senza trasformare ogni gap di coverage in un blocco.
- **F-12:** correggere bug locali semplici e default pericolosi quando si toccano le rispettive aree; evitare un refactoring generale.
- **R-01 / U-01:** una cattura RF indipendente per confermare Protected/CCMP è utile per un futuro audit, ma non blocca il gioco domestico dopo il banco funzionante.
- **R-02:** tenere traccia delle modifiche locali dell'antenna C6 per mantenere le build riproducibili; non sovrascriverle.
- **U-02:** misurare gli eventuali transitori del motore prima dell'assemblaggio definitivo, non considerarli risolti da soli controlli software.

## 3. Metodo di integrazione senza arenare lo sviluppo

- **Ora:** mantenere questo documento come decisione architetturale; proseguire lo sviluppo corrente senza impartire a Codex un incarico omnicomprensivo di correggere F-01–F-12.
- **Quando serve un gate:** creare una patch piccola e separata, con test specifici e criterio di accettazione; evitare di mischiare OTA, provisioning, Wi-Fi e driver nello stesso commit.
- **Prima del test successivo:** verificare che le sole correzioni pertinenti alla specifica prova siano completate. I rilievi non pertinenti restano in backlog.
- **Prima di dare il gioco ai bambini:** nessuna concessione sulla sicurezza elettrica/meccanica o sulla stabilità minima di alimentazione e attuatori.

## 4. Invarianti del progetto

- Non modificare automaticamente `OTA0` S3, bootloader, partition table o eFuse.
- C6: flash fisica a slot singolo `app0`, schema `no_ota`; aggiornamenti e rollback via ROM verificati.
- Nessun erase NVS come risposta automatica a un errore; mantenere i record di pairing esistenti.
- Conservare backup e credenziali private fuori da Git; verificare gli SHA-256 e le regioni attuali prima delle scritture.
- Le autorizzazioni per flash, provisioning, reset hardware, cancellazioni e merge restano **distinte ed esplicite**.
- Questo documento è **solo documentazione**, nessun cambiamento comportamentale del firmware, nessuna autorizzazione a integrare automaticamente il branch.

**Sintesi:** non correggere tutto subito, ma neppure rimandare ogni problema alla fine. Applicare correzioni funzionali e di conservazione dati prima del relativo test o utilizzo; rinviare l'hardening informatico non pertinente.
