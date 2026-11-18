# Dragon Radar OS v0.3 — sviluppo

Questo repository GitHub è uno **snapshot pulito della v0.3**. La cronologia diagnostica completa, i log di collaudo e i backup del dispositivo restano conservati soltanto nel repository locale originale.

Il progetto mantiene la base Arduino collaudata per Waveshare ESP32-S3-Touch-AMOLED-1.75-G e include un esempio firmware per Seeed XIAO ESP32-C6. DRB2 ora indirizza frame tra controller ID 0 e sfere 1–7. Moduli host verificano policy pairing, replay e ACK/retry; non sono ancora integrati nel trasporto. Pairing persistente/autenticato e cifratura unicast restano incompleti; nessun test radio S3↔C6 è stato eseguito. Non interpretare RSSI come distanza o direzione.

## Hardware e pin

Radar: ESP32-S3, AMOLED CO5300 466×466, touch CST9217, GPS LC76G, IMU QMI8658, bussola BMM150 esterna, PMIC AXP2101, motore Grove GPIO16; pulsante di test GPIO44, bus I²C SDA15/SCL14.

Sfera: XIAO ESP32-C6. Il firmware di esempio abilita il commutatore antenna esterna con GPIO3 LOW e GPIO14 HIGH prima della radio. Collegare l'antenna U.FL prima di usare questa configurazione. Il GPIO LED d'esempio è definito nello sketch; motore, buzzer e batteria sono disabilitati finché il cablaggio non viene verificato. Non collegare attuatori direttamente ai GPIO.

## Build

La build S3 usa Arduino CLI Windows richiamato da WSL, Arduino ESP32 Core 3.3.12 e il FQBN Waveshare con 16 MB flash, PSRAM OPI e partizione app3M/fat9M. La build C6 usa Arduino ESP32 Core 3.x e `esp32:esp32:XIAO_ESP32C6`.

Prerequisiti: WSL su Windows, Arduino IDE/CLI e core ESP32 già installati; Python 3; dipendenze Waveshare incluse sotto `references/libraries`; decoder Espressif con licenza e provenienza in `lib/DragonEspAudioCodec`.

```bash
./scripts/build.sh full
./scripts/build_beacon.sh 1 4 esp32:esp32:XIAO_ESP32C6
```

Le build scrivono gli artefatti sotto `build/`, esclusi da Git. Il canale `4` è un esempio: il radar e la sfera devono usare lo stesso canale effettivo. Non effettuare flash senza identificare prima la scheda e la partizione attiva. Gli script di flash del PC locale non sono inclusi nello snapshot.

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

## Privacy e stato

Non sono inclusi log diagnostici, dump/backup flash, credenziali, identificatori hardware individuali o impostazioni di porta seriale personali. Non inserire chiavi Wi-Fi, password, token o LMK nei sorgenti. Consultare [PROTOCOL_AND_POWER.md](PROTOCOL_AND_POWER.md) e [RESUME.md](RESUME.md) prima di ogni prova radio. Nessun firmware viene flashato automaticamente dai comandi di build.

Build v0.3 eseguite dalla copia pulita: S3 1.768.887 byte applicazione (su 3.145.728), 71.100 byte globali; C6 961.976 byte (su 1.310.720), 41.616 byte globali. Il branch radio include due percorsi bidirezionali separati, DEV e autenticato. I mock e le build non sostituiscono il collaudo fisico. Vedere STATUS.md per lo stato corrente.

Preparazione del primo collaudo e limiti correnti: [FIRST_RADIO_TEST.md](FIRST_RADIO_TEST.md).
