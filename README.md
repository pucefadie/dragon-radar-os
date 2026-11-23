# Dragon Radar OS v0.3 — sviluppo

Questo repository GitHub è uno **snapshot pulito della v0.3**. La cronologia diagnostica completa, i log di collaudo e i backup del dispositivo restano conservati soltanto nel repository locale originale.

Il ramo M1 integra un collegamento **DRA1 autenticato per una sfera ID 1**, con ESP-NOW unicast cifrato, handshake HMAC, anti-replay e ACK. I beacon DRB2 restano compatibili e non autenticati. DEV è un percorso separato, disattivato di default. Il banco M1c ha verificato DRA1 sul canale 6. M1d porta la compatibilità con i DRC1 installati nel firmware standard e aggiunge DRC2 con PMK casuale condivisa. Il collaudo fisico M1d resta da eseguire. Non interpretare RSSI come distanza o direzione.

[Stato e verifiche](STATUS.md) · [Procedura M1c](M1C_BENCH_TEST.md) · [Piano sicurezza](SECURITY_PLAN.md)

## Hardware e pin

Radar: ESP32-S3, AMOLED CO5300 466×466, touch CST9217, GPS LC76G, IMU QMI8658, bussola BMM150 esterna, PMIC AXP2101, motore Grove GPIO16; pulsante di test GPIO44, bus I²C SDA15/SCL14.

Sfera: XIAO ESP32-C6. Il firmware di esempio abilita il commutatore antenna esterna con GPIO3 LOW e GPIO14 HIGH prima della radio. Collegare l'antenna U.FL prima di usare questa configurazione. Il GPIO LED d'esempio è definito nello sketch; motore, buzzer e batteria sono disabilitati finché il cablaggio non viene verificato. Non collegare attuatori direttamente ai GPIO.

## Build

La build S3 usa Arduino CLI Windows richiamato da WSL, Arduino ESP32 Core 3.3.12 e il FQBN Waveshare con 16 MB flash, PSRAM OPI e partizione app3M/fat9M. La build C6 usa lo stesso core 3.3.12. Per il layout archiviato della sfera di banco usare `esp32:esp32:XIAO_ESP32C6:PartitionScheme=no_ota`, che ha un solo slot app da 2 MiB.

Prerequisiti: WSL su Windows, Arduino IDE/CLI e core ESP32 già installati; Python 3; dipendenze Waveshare incluse sotto `references/libraries`; decoder Espressif con licenza e provenienza in `lib/DragonEspAudioCodec`.

```bash
./scripts/build.sh full
DRAGON_BEACON_OUTPUT=build/c6-bench-nuova-build ./scripts/build_beacon.sh 1 6 esp32:esp32:XIAO_ESP32C6:PartitionScheme=no_ota
```

Le build scrivono gli artefatti sotto `build/`, esclusi da Git. Il canale `6` è quello previsto per questo banco: il radar e la sfera devono usare lo stesso canale effettivo. Non effettuare flash senza identificare prima la scheda e la partizione attiva. Gli script di flash del PC locale non sono inclusi nello snapshot.

Per compilare su Linux, installare Arduino CLI nativo e il core ESP32 3.3.12:

```bash
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.12 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
RADIO_DEV_INSECURE=1 ./scripts/build_linux.sh build/s3-dev-nuova-build
RADIO_DEV_INSECURE=0 ./scripts/build_linux.sh build/s3-standard-nuova-build
```

Ogni compilazione S3 Linux richiede una directory nuova e salva i binari in `artifacts/`. Lo script usa le librerie del repository e accetta `ARDUINO_CLI` per indicare un eseguibile diverso da quello nel PATH. Per isolare una build C6 Windows da snapshot precedenti, aggiungere `DRAGON_BEACON_OUTPUT=build/c6-directory-nuova` a `scripts/build_beacon.sh`.

## Test host

```bash
g++ -std=c++11 -Wall -Wextra -Werror -DRADIO_DEV_INSECURE=0 -DRADIO_RX_HOST_TEST -Itests/rx_mocks -Iinclude tests/beacon_rx_diagnostics_test.cpp src/beacon_service.cpp src/beacon_protocol.cpp src/beacon_registry.cpp -o /tmp/beacon_rx_test && /tmp/beacon_rx_test
g++ -std=c++11 -Wall -Wextra -Werror -DRADIO_DEV_INSECURE=1 -Itests/tx_mocks -Iinclude tests/dev_radio_tx_test.cpp -o /tmp/dev_radio_tx_test && /tmp/dev_radio_tx_test
g++ -std=c++11 -Wall -Wextra -Werror -Iinclude tests/dev_radio_test.cpp src/dev_radio.cpp src/beacon_protocol.cpp -o /tmp/dev_radio_test && /tmp/dev_radio_test
g++ -std=c++11 -Wall -Wextra -Werror -Iinclude tests/beacon_protocol_test.cpp src/beacon_protocol.cpp -o /tmp/protocol_test && /tmp/protocol_test
g++ -std=c++11 -Wall -Wextra -Werror -Iinclude tests/beacon_registry_test.cpp src/beacon_registry.cpp src/beacon_protocol.cpp -o /tmp/registry_test && /tmp/registry_test
g++ -std=c++11 -Wall -Wextra -Werror -Iinclude tests/radio_link_test.cpp src/radio_link.cpp src/beacon_protocol.cpp -o /tmp/radio_link_test && /tmp/radio_link_test
g++ -std=c++11 -Wall -Wextra -Werror -Iinclude tests/secure_radio_test.cpp src/secure_radio.cpp src/radio_link.cpp -lcrypto -o /tmp/secure_radio_test && /tmp/secure_radio_test
g++ -std=c++11 -Wall -Wextra -Werror -Iinclude tests/proximity_engine_test.cpp src/proximity_engine.cpp -o /tmp/proximity_test && /tmp/proximity_test
python3 tests/make_radio_records_test.py
```

Il test DRA1 richiede le intestazioni di sviluppo OpenSSL per verificare lo stesso HMAC-SHA-256 usato dal firmware.

Per verificare anche le azioni della fixture opzionale M1c sullo stesso codice:

```bash
g++ -std=c++11 -Wall -Wextra -Werror -DM1C_BENCH_MODE=1 -DM1C_BENCH_FIXTURE=1 -Iinclude tests/secure_radio_test.cpp src/secure_radio.cpp src/radio_link.cpp -lcrypto -o /tmp/secure_radio_fixture_test && /tmp/secure_radio_fixture_test
```

L'utility USB si può verificare su host con una NVS simulata.

```bash
g++ -std=c++11 -Wall -Wextra -Werror -Itests/provision_mocks -Iinclude tests/secure_radio_provision_test.cpp src/secure_radio.cpp src/radio_link.cpp -o /tmp/secure_radio_provision_test
for case_name in install legacy exists lookup-error commit-error; do
  /tmp/secure_radio_provision_test "$case_name" || exit 1
done
```

## Privacy e stato

Non sono inclusi log diagnostici, dump/backup flash, credenziali, identificatori hardware individuali o impostazioni di porta seriale personali. Non inserire chiavi Wi-Fi, password, token o LMK nei sorgenti. Consultare [PROTOCOL_AND_POWER.md](PROTOCOL_AND_POWER.md) e [RESUME.md](RESUME.md) prima di ogni prova radio. Nessun firmware viene flashato automaticamente dai comandi di build.

Le verifiche e gli hash correnti sono in [STATUS.md](STATUS.md). I file generati e i backup di banco restano fuori da Git.
