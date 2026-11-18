# Dragon Radar OS — stato M1

**Branch:** `feat/espnow-m1-bidirectional` · **Baseline M1b:** `a43bfe7` · **Sorgenti firmware:** `7ff019d` · **PR #2:** draft. Revisione AI della issue #3 verificata al checkpoint `a43bfe7`.

| Stato | Evidenza |
|---|---|
| **PASS — M1b software** | DRA1 bidirezionale per ID 1 con ESP-NOW unicast cifrato, handshake HMAC, replay window, ACK/retry e utility cablata per NVS. Modalità DEV separata. Nove gruppi host e sei build erano passati al [checkpoint M1b](https://github.com/pucefadie/dragon-radar-os/blob/a43bfe7/STATUS.md). |
| **PASS — preparazione M1c** | Rieseguiti otto test C++ e il test del generatore con record sintetici. Verificati hash dei sei binari M1b, coppie dei backup integrali S3/C6, layout e fit delle immagini app per il banco. Bootstrap S3 storico con rollback attivo. Build C6 standard e provisioning `no_ota` riuscite e corrispondenti alla tabella C6 archiviata. |
| **FAIL — profilo C6 M1b rispetto alla scheda** | Le immagini C6 M1b erano valide per il proprio profilo a due slot, ma quella tabella non corrisponde a quella archiviata per il C6 fisico. Usare solo i nuovi candidati app C6 `no_ota`, previa verifica corrente. |
| **PASS storico — DRB2 fisico, solo ricezione** | Ricevitore S3 minimale: 10 beacon validi confermati sul canale 6, senza errori CRC nei report completi, NVS immutata. Al termine OTA1 selezionato con sequenza 4 VALID, OTA0 integro. Non è una prova DRA1. |
| **NOT RUN — M1c fisico** | Nessun contatto con le schede in questo ciclo. Restano da verificare stato corrente, isolamento attuatori, backup live e successive prove di provisioning, DRA1, ACK, replay, perdita e recupero. I casi negativi fisici con ACK soppresso, tag alterato e replay richiedono una fixture dedicata. |

## Immagini applicative per il banco

Tutti i file restano locali e non sono stati programmati. S3 con profilo esistente a due slot, C6 `PartitionScheme=no_ota` con antenna esterna e canale 6 per l'app standard. Entrambi core Arduino ESP32 3.3.12. L'offset e la selezione formano un piano condizionato alla lettura corrente, non un'autorizzazione.

| Immagine | Byte | SHA-256 |
|---|---:|---|
| S3 standard, da M1b | 1782000 | `d8e31f154e1e51c4d0636d37b2b949c29b96d4ea4f0631e60351461bfd057a9f` |
| S3 provisioning, da M1b | 312112 | `7601bc33f7613721085b4135f344319289d5e8b6a5976e1b8e873cceca6f3eb5` |
| C6 standard no_ota | 970880 | `89ec01d90823313771fe393489384f512bd295670d7f7f1630a5563e19f9b61f` |
| C6 provisioning no_ota | 263008 | `705a737d66416b8fff38a276d79981177f50ea5f208485db3603b931044b7d64` |

## Prossime tre azioni

1. Predisporre banco USB senza batteria, antenna U.FL collegata e attuatori isolati. Sul S3 verificare l'isolamento fisico del motore o approvare un'immagine di banco senza azionamenti.
2. Nella sessione autorizzata eseguire il preflight live, acquisire i backup aggiornati e validare un piano di scrittura e rollback per ciascuna scheda. Il backup integrale storico C6 precede l'app DEV osservata per ultima.
3. Dopo autorizzazione distinta generare i record temporanei, installare e verificare i candidati e collaudare DRA1. NVS non cifrata accettata solo al banco. Preparare prima la fixture per le prove negative, senza confonderle con le verifiche host.

[M1C_BENCH_TEST.md](M1C_BENCH_TEST.md) · [SECURITY_PLAN.md](SECURITY_PLAN.md). **M1 resta aperta** e la PR non va mergiata.
